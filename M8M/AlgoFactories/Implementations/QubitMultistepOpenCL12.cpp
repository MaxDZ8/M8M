/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "QubitMultistepOpenCL12.h"


const auint QubitMultistepOpenCL12::MAX_CONCURRENCY = 1024 * 32 * 32;


asizei QubitMultistepOpenCL12::ValidateSettings(OpenCL12Wrapper &cl, const std::vector<Settings::ImplParam> &params) {
	Options building;
    for(auto el = params.cbegin(); el != params.cend(); ++el) {
		if(!_stricmp("linearIntensity", el->name.c_str())) {
			if(el->valueUINT == 0) throw std::string("Invalid linearIntensity (0, would imply no work being carried out).");
			building.linearIntensity = el->valueUINT;
		}
		else if(!_stricmp("dispatchCount", el->name.c_str())) {
			if(el->valueUINT == 0) throw std::string("Invalid dispatch count (0, would imply no work being carried out).");
			building.dispatchCount = el->valueUINT;
		}
		else throw std::string("Unrecognized option \"") + el->name + "\" for OpenCL.scrypt1024.multistep kernel.";
	}
	if(building.Concurrency() > MAX_CONCURRENCY) throw std::string("Concurrency is too high (") + std::to_string(building.Concurrency()) + ", max value is " + std::to_string(MAX_CONCURRENCY) + '.';
	
    const cl_device_id use = cl.GetLessUsedDevice();
	Resources add;
	cl_int error;
	cl_context_properties platform[] = { CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(cl.GetPlatform(use)), 0, 0 }; // a single zero would suffice
	add.ctx = clCreateContext(platform, 1, &use, errorCallback, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create context.";

	cl_command_queue_properties cqprops = PROFILING_ENABLED? CL_QUEUE_PROFILING_ENABLE : 0;
	add.commandq = clCreateCommandQueue(add.ctx, use, cqprops, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create command queue.";

	size_t byteCount = 128; // 80+32=112 used but be multiple of uint4 like D3D11
	add.wuData = clCreateBuffer(add.ctx, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create wuData buffer.";
	byteCount = 5 * sizeof(cl_uint);
	add.dispatchData = clCreateBuffer(add.ctx, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create dispatchData buffer.";

	{
		auint lut[4][256];
		byteCount = sizeof(lut);
		aes::RoundTableRowZero(lut[0]);
		for(asizei i = 0; i < 256; i++) lut[1][i] = _rotl(lut[0][i], 8);
		for(asizei i = 0; i < 256; i++) lut[2][i] = _rotl(lut[1][i], 8);
		for(asizei i = 0; i < 256; i++) lut[3][i] = _rotl(lut[2][i], 8);

		cl_int error;
		add.aesLUTTables = clCreateBuffer(add.ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(lut), lut, &error);
		if(error) throw std::exception("could not init AES LUT buffer.");
	}
	{
		// The ALPHA table contains (41^n) % 257, with n [0..255]. Due to large powers, you might thing this is a huge mess
		// but it really isn't due to modulo properties. More information can be found in SIMD documentation from
		// Ecole Normale Superieure, webpage of Gaetan Laurent, you need to look for the "Full Submission Package", will end up
		// with a file SIMD.zip, containing reference.c which explains what to do at LN121.
		// Anyway, the results of the above operations are MOSTLY 8-bit numbers. There's an exception however.
		// alphaValue[128] is 0x0100. I cut it easy and make everything a short.
		short alphaValue[256];
		const int base = 41; 
		int power = 1; // base^n
		for(int loop = 0; loop < 256; loop++) {
			if((power & 0x0000FFFF) != power) std::cout<<loop<<std::endl;
			alphaValue[loop] = short(power);
			power = (power * base) % 257;
		}
		cl_int error;
		add.alphaTable = clCreateBuffer(add.ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(alphaValue), alphaValue, &error);
		if(error) throw std::exception("could not init AES LUT buffer.");
	}
	{
		// The BETA table is very similar. It is built in two steps. In the first, it is basically an alpha table with a different base...
		unsigned short betaValue[256];
		int base = 163;  // according to documentation, this should be "alpha^127 (respectively alpha^255 for SIMD-256)" which is not 0xA3 to me but I don't really care.
		int power = 1; // base^n
		for(int loop = 0; loop < 256; loop++) {
			if((power & 0x0000FFFF) != power) std::cout<<loop<<std::endl;
			betaValue[loop] = static_cast<unsigned short>(power);
			power = (power * base) % 257;
		}
		// Now reference implementation mangles it again adding the powers of 40^n,
		// but only in the "final" message expansion. So we need to do nothing more.
		// For some reason the beta value table is called "yoff_b_n" in legacy kernels by lib-SPH... 
		cl_int error;
		add.betaTable = clCreateBuffer(add.ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(betaValue), betaValue, &error);
		if(error) throw std::exception("could not init AES LUT buffer.");

	}

	for(auint i = 0; i < add.io.size(); i++) {
		add.io[i] = clCreateBuffer(add.ctx, CL_MEM_HOST_NO_ACCESS, building.HashesPerDispatch() * 16 * sizeof(cl_uint), NULL, &error);
		if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create I/O buffer " + std::to_string(i) + ".";
	}
	add.nonces = clCreateBuffer(add.ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, (1 + building.OptimisticNonceCountMatch()) * sizeof(cl_uint), NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting nonces buffer.";

#if DEBUGBUFFER_SIZE
	add.debugBUFFER = clCreateBuffer(add.ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, DEBUGBUFFER_SIZE, NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting debug buffer.";
#endif

	std::string filename;
	std::unique_ptr<char[]> src;
	asizei srcLen = 0;
	auint pieces = 0;
	for(; (filename = GetSourceFileName(building, pieces)).length(); pieces++) {
		filename = "kernels/" + filename;
		if(!LoadFile(src, srcLen, filename)) throw std::string("OpenCL code must be loaded and compiled explicitly, but \"") + filename + "\" cannot be found.";
		const char *source = src.get();
		add.program[pieces] = clCreateProgramWithSource(add.ctx, 1, &source, &srcLen, &error);
		if(!add.program[pieces] || error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateProgramWithSource.";
		const std::string defines(GetBuildOptions(building, pieces));
		error = clBuildProgram(add.program[pieces], 1, &use, defines.c_str(), NULL, NULL); // blocking build
		ProbeProgramBuildInfo(add.program[pieces], use, defines);
		if(error != CL_SUCCESS) {
			throw std::string("OpenCL error ") + std::to_string(error) + " returned by clBuildProgram.";
		}

		add.kernel[pieces] = clCreateKernel(add.program[pieces], GetKernelName(building, pieces).c_str(), &error);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateKernel while trying to create " + GetKernelName(building, pieces);

		cl_uint args = 0;
		cl_mem input;
		if(pieces == 0) {
			cl_mem target[] = { add.wuData, add.io[0], nullptr };
			const char *nameString[] = { "wuData", "io0" };
			input = target[0];
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
		}
		else if(pieces == 4) {
			cl_mem target[] = { add.io[1], add.nonces, add.dispatchData, nullptr };
			const char *nameString[] = { "io1", "nonces", "dispatchData" };
			input = target[0];
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
		}
		else {
			cl_mem target[] = { add.io[1], add.io[0], nullptr };
			const char *nameString[] = { "io1", "io0" };
			if(pieces % 2) {
				std::swap(nameString[0], nameString[1]);
				std::swap(target[0], target[1]);
			}
			input = target[0];
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
		}
		switch(pieces) {
		case 2:
		case 4: {
			error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(add.aesLUTTables), &add.aesLUTTables);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on AES round LUT buffer.";
			args++;
			break;
		}
		case 3: {
			cl_mem target[] = { input, add.alphaTable, add.betaTable, nullptr };
			const char *nameString[] = { "inputCHAR", "alpha LUT", "beta LUT" };
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], args, sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
			break;
		}
		}
#if DEBUGBUFFER_SIZE
		error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(add.debugBUFFER), &add.debugBUFFER);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on \"debugBUFFER\"";
#endif
	}
	// {
	// 	pieces--; // Last step is a bit more complicated.
	// 	cl_mem target[] = { add.io[0], add.nonces, add.dispatchData, nullptr };
	// 	const char *nameString[] = { "io0", "nonces", "dispatchData" };
	// 	for(asizei loop = 0; target[loop]; loop++) {
	// 		error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(target[loop]), target + loop);
	// 		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
	// 	}
	// #if DEBUGBUFFER_SIZE
	// 		error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(add.debugBUFFER), &add.debugBUFFER);
	// 		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on \"debugBUFFER\"";
	// #endif
	// }
	algoInstances.push_back(AlgoInstance());
	AlgoInstance &keep(algoInstances.back());
	keep.options = building;
	keep.res = std::move(add);
	return algoInstances.size() - 1;
}


//! \todo This should really go away, it's just easier to have everything in the ValidateSettings which should get a new name. InitWithSettings()?
bool QubitMultistepOpenCL12::Ready(asizei resourceSlot) const {
	return true;
}


//! \todo What was this even supposed to do? Called when Ready returns false... for hot switching algos? Consider simplification.
void QubitMultistepOpenCL12::Prepare(asizei resourceSlot) {
	/* * * * * * * * * * * * */
	throw std::string("TODO");
	/* * * * * * * * * * * * */
}


void QubitMultistepOpenCL12::Dispatch() {
#if DEBUGBUFFER_SIZE
	auto UintHexOut = [](std::ofstream &out, aubyte *bytes, asizei count, bool splitting) -> aubyte* {
		count *= 4;
		const char *hex = "0123456789abcdef";
		for(asizei loop = 0; loop < count; loop++) {
			if(splitting && loop && (loop % 4) == 0) out<<' ';
			char hi = bytes[loop] >> 4;
			char lo = bytes[loop] & 0x0F;
			out<<hex[hi]<<hex[lo];
		}
		return bytes + count;
	};
	// Work items in SIMD work transposed.
	auto UnpackHexOut = [&UintHexOut](std::ofstream &out, aubyte *bytes, asizei count, bool splitting) -> aubyte* {
		for(asizei loop = 0; loop < count; loop++) {
			asizei x = loop / 16;
			asizei y = loop % 16;
			UintHexOut(out, bytes + (y * 16 + x) * 4, 1, splitting);
		}
		return bytes + count * 4;
	};
#endif
	for(asizei loop = 0; loop < algoInstances.size(); loop++) {
		AlgoInstance &use(algoInstances[loop]);
		switch(use.step) {
		case 1: {
			const asizei woff[1] = { use.nonceBase };
			const asizei workDim [1] = { use.options.HashesPerDispatch() };
			const asizei groupDim[1] = { 256 }; // based on observation from profiling slightly better.
			cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[0], 1, woff, workDim, groupDim, 0, NULL, NULL);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[0] kernel.";
			
#if DEBUGBUFFER_SIZE
			{
				const asizei start = 0;
				const asizei size = 20 + 16;
				cl_uint hostBuff[size];
				error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
				if(error != CL_SUCCESS) { return; }

				std::ofstream dbg("debug_LUFFA_OUT.txt");
				unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
				const char *hex = "0123456789abcdef";
				for(asizei loop = 0; loop < size * sizeof(hostBuff[0]); loop++) {
					if(loop == 20 * sizeof(hostBuff[0])) dbg<<std::endl;
					char hi = bytes[loop] >> 4;
					char lo = bytes[loop] & 0x0F;
					dbg<<hex[hi]<<hex[lo];
				}
			}
#endif


			break;
		}
		case 2: {
			const asizei woff[2] = { 0, use.nonceBase };
			const asizei workDim [2] = { 2, use.options.HashesPerDispatch() };
			const asizei groupDim[2] = { 2, 32 }; // required by 2-way kernel.
			cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[1], 2, woff, workDim, groupDim, 0, NULL, NULL);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[1] kernel.";

#if DEBUGBUFFER_SIZE
			{
				const asizei start = 0;
				const asizei size = 16 + // luffa output
					                16; // cubehash output
				cl_uint hostBuff[size];
				error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
				if(error != CL_SUCCESS) { return; }

				unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
				const char *hex = "0123456789abcdef";
				asizei dump = 0;
				{
					std::ofstream dbg("debug_LUFFA_OUT.txt", std::ios::app);
					dbg<<std::endl;
					for(; dump < 16 * 4; dump++) {
						char hi = bytes[dump] >> 4;
						char lo = bytes[dump] & 0x0F;
						dbg<<hex[hi]<<hex[lo];
					}
				}
				{
					std::ofstream dbg("debug_CUBEHASH_OUT.txt");
					for(; dump < size * sizeof(hostBuff[0]); dump++) {
						char hi = bytes[dump] >> 4;
						char lo = bytes[dump] & 0x0F;
						dbg<<hex[hi]<<hex[lo];
					}
				}
			}
#endif
			break;
		}
		case 3: {
			const asizei woff[1] = { use.nonceBase };
			const asizei workDim [1] = { use.options.HashesPerDispatch() };
			const asizei groupDim[1] = { 64 }; // required workgroup size... but perhaps 128-256 would be a better idea due to cache trashing
			cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[2], 1, woff, workDim, groupDim, 0, NULL, NULL);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[2] kernel.";

#if DEBUGBUFFER_SIZE
			{
				const asizei start = 0;
				const asizei size = 16 + // cubehash output
					                16; // shavite3 output
				cl_uint hostBuff[size];
				error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
				if(error != CL_SUCCESS) { return; }

				unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
				const char *hex = "0123456789abcdef";
				asizei dump = 0;
				{
					std::ofstream dbg("debug_CUBEHASH_OUT.txt", std::ios::app);
					dbg<<std::endl;
					for(; dump < 16 * 4; dump++) {
						char hi = bytes[dump] >> 4;
						char lo = bytes[dump] & 0x0F;
						dbg<<hex[hi]<<hex[lo];
					}
				}
				{
					std::ofstream dbg("debug_SHAVITE3_OUT.txt");
					asizei end = dump + 4 * 16;
					for(; dump < end; dump++) {
						char hi = bytes[dump] >> 4;
						char lo = bytes[dump] & 0x0F;
						dbg<<hex[hi]<<hex[lo];
					}
				}
			}
#endif
			break;
		}
		case 4: {
			const asizei woff[2] = { 0, use.nonceBase };
			const asizei workDim [2] = { 16, use.options.HashesPerDispatch() };
			const asizei groupDim[2] = { 16, 4 };
			cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[3], 2, woff, workDim, groupDim, 0, NULL, NULL);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[3] kernel.";

#if DEBUGBUFFER_SIZE
			{
				const asizei start = 0;
				const asizei size = 16 + // shavite3 output
					                16; // SIMD output
				cl_uint hostBuff[size];
				error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
				if(error != CL_SUCCESS) { return; }

				unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
				{
					std::ofstream dbg("debug_SHAVITE3_OUT.txt", std::ios::app);
					dbg<<std::endl;
					bytes = UintHexOut(dbg, bytes, 16, false);
				}
				{
					std::ofstream dbg("debug_SIMD_OUT.txt");
					bytes = UintHexOut(dbg, bytes, 16, false);
				}
			}
#endif
			break;
		}
		case 5: {
			const asizei woff[2] = { 0, use.nonceBase };
			const asizei workDim [2] = { 8, use.options.HashesPerDispatch() };
			const asizei groupDim[2] = { 8, 8 };
			cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[4], 2, woff, workDim, groupDim, 0, NULL, NULL);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[4] kernel.";
			const asizei size = (1 + use.options.OptimisticNonceCountMatch()) * sizeof(cl_uint);
			void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.nonces, CL_FALSE, CL_MAP_READ, 0, size, 0, NULL, &use.res.resultsTransferred, &error);
			use.res.mappedNonceBuff = reinterpret_cast<cl_uint*>(raw);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueMapBuffer while mapping result nonce buffer.";
#if DEBUGBUFFER_SIZE
			{
				const asizei start = 0;
				const asizei size = 16 + // SIMD output
					                16+16+16; // ECHO output
				cl_uint hostBuff[size];
				error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
				if(error != CL_SUCCESS) { return; }

				unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
				{
					std::ofstream dbg("debug_SIMD_OUT.txt", std::ios::app);
					dbg<<std::endl;
					bytes = UintHexOut(dbg, bytes, 16, false);
				}
				{
					std::ofstream dbg("debug_ECHO_OUT.txt");
					dbg<<"ECHO hash out"<<std::endl;
					bytes = UintHexOut(dbg, bytes, 16, false);
					dbg<<"\nECHO target"<<std::endl;
					bytes = UintHexOut(dbg, bytes, 16, false);
				}
			}
#endif
			break;
		}
		default: use.step--;
		}
		use.step++;
	}
}


void QubitMultistepOpenCL12::HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei internalIndex) {
	/* * * * * * * * * * * * */
	//! \todo Implement qubit CPU-side hashing for validation.
	throw std::string("TODO");
	/* * * * * * * * * * * * */
}


QubitMultiStep_Resources::QubitMultiStep_Resources() {
	memset(this, 0, sizeof(*this));
}


QubitMultiStep_Resources::~QubitMultiStep_Resources() {
	// what if the nonce buffer is still mapped? I destroy both the event and the buffer anyway.
	if(resultsTransferred) clReleaseEvent(resultsTransferred);
	for(asizei i = 0; i < kernel.size(); i++) if(kernel[i]) clReleaseKernel(kernel[i]);
	for(asizei i = 0; i < program.size(); i++) if(program[i]) clReleaseProgram(program[i]);
	for(asizei i = 0; i < io.size(); i++) if(io[i]) clReleaseMemObject(io[i]);
	if(aesLUTTables) clReleaseMemObject(aesLUTTables);
	if(alphaTable) clReleaseMemObject(alphaTable);
	if(betaTable) clReleaseMemObject(betaTable);
	if(nonces) clReleaseMemObject(nonces);
	if(dispatchData) clReleaseMemObject(dispatchData);
	if(wuData) clReleaseMemObject(wuData);
	if(commandq) clReleaseCommandQueue(commandq);
	if(ctx) clReleaseContext(ctx);
}

#define COPYCLEAR(FIELD) FIELD = other.FIELD; other.FIELD = 0;

QubitMultiStep_Resources::QubitMultiStep_Resources(QubitMultiStep_Resources &&other) { // move semantics makes everything easier!
	COPYCLEAR(resultsTransferred);
	for(asizei i = 0; i < kernel.size(); i++) { kernel[i] = other.kernel[i];    other.kernel[i] = 0; }
	for(asizei i = 0; i < program.size(); i++) { program[i] = other.program[i];    other.program[i] = 0; }
	for(asizei i = 0; i < io.size(); i++) { io[i] = other.io[i];    other.io[i] = 0; };
	COPYCLEAR(aesLUTTables);
	COPYCLEAR(alphaTable);
	COPYCLEAR(betaTable);
	COPYCLEAR(nonces);
	COPYCLEAR(dispatchData);
	COPYCLEAR(wuData);
	COPYCLEAR(commandq);
	COPYCLEAR(ctx);

#if DEBUGBUFFER_SIZE
	COPYCLEAR(debugBUFFER);
#endif
	//#undef COPYCLEAR
}


QubitMultiStep_Resources& QubitMultiStep_Resources::operator=(QubitMultiStep_Resources &&other) { // move semantics makes everything easier!
	COPYCLEAR(resultsTransferred);
	for(asizei i = 0; i < kernel.size(); i++) { kernel[i] = other.kernel[i];    other.kernel[i] = 0; }
	for(asizei i = 0; i < program.size(); i++) { program[i] = other.program[i];    other.program[i] = 0; }
	for(asizei i = 0; i < io.size(); i++) { io[i] = other.io[i];    other.io[i] = 0; };
	COPYCLEAR(aesLUTTables);
	COPYCLEAR(alphaTable);
	COPYCLEAR(betaTable);
	COPYCLEAR(nonces);
	COPYCLEAR(dispatchData);
	COPYCLEAR(wuData);
	COPYCLEAR(commandq);
	COPYCLEAR(ctx);

#if DEBUGBUFFER_SIZE
	COPYCLEAR(debugBUFFER);
#endif
	return *this;
}

#undef COPYCLEAR


//! \todo very ugly code repetition from Scrypt1024_Multistep_OpenCL12, need a base class for CL12
bool QubitMultistepOpenCL12::LoadFile(std::unique_ptr<char[]> &source, asizei &len, const std::string &name) {
	len = 0;
	std::ifstream disk(name.c_str(), std::ios_base::binary);
	if(disk.good() == false) return false;
	disk.seekg(0, std::ios_base::end);
	aulong size = disk.tellg();
	const asizei maxSize = 1024 * 1024 * 8;
	if(size > maxSize) throw std::string("File ") + name + " is way too big: " + std::to_string(size) + "B, max is " + std::to_string(maxSize) + "B.";
	disk.seekg(0, std::ios_base::beg);
	// CL2 is clear on the fact the strings don't need to be NULL-terminated. CL1.2 was rather clear in rationale as well, but it looks like
	// some AMD APP versions really want this string NULL-terminated.
	source.reset(new char[asizei(size + 1)]);
	disk.read(source.get(), size);
	source.get()[size] = 0;
	len = asizei(size);
	return true;
}


void QubitMultistepOpenCL12::ProbeProgramBuildInfo(cl_program program, cl_device_id device, const std::string &defines) const {
	cl_build_status status;
	cl_int error = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_STATUS, sizeof(status), &status, NULL);
	if(status == CL_BUILD_NONE || status == CL_BUILD_IN_PROGRESS) return;
	bool fatal = status == CL_BUILD_ERROR;
	asizei count = 0;
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &count);
	std::unique_ptr<char[]> messages(new char[count]);
	clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, count, messages.get(), NULL);
	if(messages.get() && count > 1) errorCallback("Program build log:\n", messages.get(), strlen(messages.get()), NULL);
	if(fatal) throw std::string("Fatal error while building program, see output. Compiling with\n" + defines);
}


std::string QubitMultistepOpenCL12::GetBuildOptions(const Options &opt, const auint index) {
	std::stringstream build;
	switch(index) {
	case 0: build<<"-D LUFFA_HEAD"; break;
	case 1: break;
	case 2: break;
	case 3: break;
	case 4: build<<"-D AES_TABLE_ROW_1 -D AES_TABLE_ROW_2 -D AES_TABLE_ROW_3 -D ECHO_IS_LAST "; break;
	}
#if defined(_DEBUG)
	//build<<" -g"; //cl2.0 only, even though CodeXL wants it!
#endif
	// CL2.0 //
	//build<<" -cl-uniform-work-group-size";
	return build.str();
}


std::string QubitMultistepOpenCL12::GetSourceFileName(const Options &opt, auint index) {
	switch(index) {
	case 0: return std::string("Luffa_1W.cl");
	case 1: return std::string("CubeHash_2W.cl");
	case 2: return std::string("SHAvite3_1W.cl");
	case 3: return std::string("SIMD_16W.cl");
	case 4: return std::string("Echo_8W.cl");
	}
	return std::string();
}


std::string QubitMultistepOpenCL12::GetKernelName(const Options &opt, auint index) {
	switch(index) {
	case 0: return std::string("Luffa_1way");
	case 1: return std::string("CubeHash_2way");
	case 2: return std::string("SHAvite3_1way");
	case 3: return std::string("SIMD_16way");
	case 4: return std::string("Echo_8way");
	}
	return std::string();

}
