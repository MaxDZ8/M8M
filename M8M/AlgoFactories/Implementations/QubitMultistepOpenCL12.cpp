/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
 
/* Adding a note since it seems this file attracts a lot of readers for some reason.
The assumptions made while writing this file have turned out to be **false**.
So, don't even try understanding **this nonsense**. A better way of doing the same thing can be found in Fresh,
which is basically the same thing. The idea here was to be able to dispatch multiple concurrent algos.
It is unnecessary.

Next M8M version will feature an overhauled, consistently more compact algorithm structure.

Also, kernel validity checking is now no more here. The validity checking project has been overhauled as well
and published @ https://github.com/MaxDZ8/oclcckvck and now meant to be deployable to end-users.
*/


#include "QubitMultistepOpenCL12.h"


bool QubitMultistepOpenCL12::Dispatch(asizei setIndex, asizei slotIndex) {
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
	QubitMultiStep_Options &options(settings[setIndex].options);
	AlgoInstance &use(settings[setIndex].algoInstances[slotIndex]);
	switch(use.step) {
	case 1: {
		const asizei woff[1] = { use.nonceBase };
		const asizei workDim [1] = { options.HashCount() };
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
		const asizei workDim [2] = { 2, options.HashCount() };
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
		const asizei workDim [1] = { options.HashCount() };
		const asizei groupDim[1] = { 64 }; // 64 is required as profiler told me wsize 64 forced is very slighly better
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
		const asizei workDim [2] = { 16, options.HashCount() };
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
		const asizei workDim [2] = { 8, options.HashCount() };
		const asizei groupDim[2] = { 8, 8 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[4], 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to qubit[4] kernel.";
		const asizei size = (1 + options.OptimisticNonceCountMatch()) * sizeof(cl_uint);
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
	return use.step <= 5;
}


void QubitMultistepOpenCL12::HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei resIndex) {
	/* * * * * * * * * * * * */
	//! \todo Implement qubit CPU-side hashing for validation.
	throw std::string("TODO: qubit hash validation not implemented yet. I cannot be bothered with it!");
	/* * * * * * * * * * * * */
}


QubitMultistepOpenCL12::~QubitMultistepOpenCL12() {
	// what if the nonce buffer is still mapped? I destroy both the event and the buffer anyway.
	for(asizei s = 0; s < settings.size(); s++) {
		for(asizei i = 0; i < settings[s].algoInstances.size(); i++) settings[s].algoInstances[i].res.Free();
	}
}


void QubitMultistepOpenCL12::GetSettingsInfo(SettingsInfo &out, asizei setting) const {
	// In line of theory I could design some fairly contrived way to do everything using GenResources...
	// BUT I would end mixing logic and presentation code. Not a good idea. Since algo implementations don't change very often,
	// I think it's a better idea to just keep them in sync.
	const auto &opt(settings[setting].options);
	out.hashCount = opt.HashCount();
	std::vector<std::string> constQual, inoutQual, outQual;
	constQual.push_back(std::string("const"));
	inoutQual.push_back(std::string("in"));
	inoutQual.push_back(std::string("out"));
	outQual.push_back(std::string("out"));

	const std::string deviceRes("device");
	const std::string hostRes("host");

	out.Push(deviceRes, constQual, "work unit data",   128);
	out.Push(deviceRes, constQual, "dispatch info",    5 * sizeof(cl_uint));
	out.Push(deviceRes, constQual, "AES round tables", sizeof(auint) * 4 * 256);
	out.Push(deviceRes, constQual, "SIMD alpha table", sizeof(short) * 256);
	out.Push(deviceRes, constQual, "SIMD beta table",  sizeof(unsigned short) * 256);
	out.Push(deviceRes, inoutQual, "Hash IO buffers (2)", sizeof(cl_uint) * 16 * opt.HashCount());
	out.Push(hostRes,   outQual,   "found nonces ",    sizeof(cl_uint) * (1 + opt.OptimisticNonceCountMatch()));
}


std::string QubitMultistepOpenCL12::GetBuildOptions(const QubitMultiStep_Options &opt, const auint index) {
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



std::pair<std::string, std::string> QubitMultistepOpenCL12::GetSourceFileAndKernel(asizei settingIndex, auint step) const {
    using std::string;
    switch(step) {
	case 0: return std::make_pair(string("Luffa_1W.cl"), string("Luffa_1way"));
	case 1: return std::make_pair(string("CubeHash_2W.cl"), string("CubeHash_2way"));
	case 2: return std::make_pair(string("SHAvite3_1W.cl"), string("SHAvite3_1way"));
	case 3: return std::make_pair(string("SIMD_16W.cl"), string("SIMD_16way"));
	case 4: return std::make_pair(string("Echo_8W.cl"), string("Echo_8way"));
    }
    string empty;
    return std::make_pair(empty, empty);
}


void QubitMultistepOpenCL12::Parse(QubitMultiStep_Options &opt, const rapidjson::Value &params) {
	const rapidjson::Value::ConstMemberIterator li(params.FindMember("linearIntensity"));
	if(li != params.MemberEnd() && li->value.IsUint() && li->value.GetUint()) opt.linearIntensity = li->value.GetUint();
}


asizei QubitMultistepOpenCL12::ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback) {
	//! \todo support multiple configurations.
	bool fail = false;
	auto badthing = [&callback, &fail](const char *str) { fail = true;    if(callback) callback(str); };

	if(OpenCL12Wrapper::GetDeviceGroupInfo(plat, OpenCL12Wrapper::dgis_profile) != "FULL_PROFILE") badthing("full profile required");
	if(dev.type.gpu == false) badthing("must be GPU");

	const std::string extensions = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::dis_extensions);
	const auint vid = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::diu_vendorID);
	const std::string chip = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::dis_chip);
	auto chipType = dev.type.gpu? KnownHardware::ct_gpu : KnownHardware::ct_cpu;
	KnownHardware::Architecture arch = KnownHardware::GetArchitecture(vid, chip.c_str(), chipType, extensions.c_str());
	if(arch < KnownHardware::arch_gcn_first || arch > KnownHardware::arch_gcn_last) badthing("must be AMD GCN");

    // Evaluate max concurrency. It mandates memory usage (constant) and a real hardware concurrency which is up to 16*HashCount (SIMD is 16-way)
	const asizei buffBytes = settings[0].options.HashCount() * 16 * sizeof(cl_uint); // intermediate hashes are 512bit -> 16 uints
    const aulong maxBuffBytes = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::diul_maxMemAlloc);
    if(buffBytes > maxBuffBytes) {
        std::string err("intensity too high, max buffer size is ");
        err += std::to_string(maxBuffBytes) + " Bytes";
        badthing(err.c_str());
    }

	if(fail) return settings.size();

	return 0; // ok if we support a single config.
}


void QubitMultistepOpenCL12::BuildDeviceResources(QubitMultiStep_Resources &add, cl_context ctx, cl_device_id use, const QubitMultiStep_Options &opt, asizei confIndex) {
	cl_int error;
	cl_command_queue_properties cqprops = PROFILING_ENABLED? CL_QUEUE_PROFILING_ENABLE : 0;
	add.commandq = clCreateCommandQueue(ctx, use, cqprops, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create command queue.";

	size_t byteCount = 128; // 80+32=112 used but be multiple of uint4 like D3D11
	add.wuData = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create wuData buffer.";
	byteCount = 5 * sizeof(cl_uint);
	add.dispatchData = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create dispatchData buffer.";

	{
		auint lut[4][256];
		byteCount = sizeof(lut);
		aes::RoundTableRowZero(lut[0]);
		for(asizei i = 0; i < 256; i++) lut[1][i] = _rotl(lut[0][i], 8);
		for(asizei i = 0; i < 256; i++) lut[2][i] = _rotl(lut[1][i], 8);
		for(asizei i = 0; i < 256; i++) lut[3][i] = _rotl(lut[2][i], 8);

		cl_int error;
		add.aesLUTTables = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(lut), lut, &error);
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
		add.alphaTable = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(alphaValue), alphaValue, &error);
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
		add.betaTable = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(betaValue), betaValue, &error);
		if(error) throw std::exception("could not init AES LUT buffer.");

	}

	for(auint i = 0; i < add.io.size(); i++) {
		add.io[i] = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, opt.HashCount() * 16 * sizeof(cl_uint), NULL, &error);
		if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create I/O buffer " + std::to_string(i) + ".";
	}
	add.nonces = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, (1 + opt.OptimisticNonceCountMatch()) * sizeof(cl_uint), NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting nonces buffer.";

#if DEBUGBUFFER_SIZE
	add.debugBUFFER = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, DEBUGBUFFER_SIZE, NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting debug buffer.";
#endif

    std::vector<char> src;
	auint pieces = 0;
	//! \todo in a multi-device situation we would really be reloading the same stuff from disk multiple times. I really must build a temporary cache for source!
    std::pair<std::string, std::string> fikern;
	for(; (fikern = GetSourceFileAndKernel(confIndex, pieces)).first.length(); pieces++) {
		fikern.first = "kernels/" + fikern.first;
		if(!GetSourceFromFile(src, fikern.first)) throw std::string("OpenCL code must be loaded and compiled explicitly, but \"") + fikern.first + "\" cannot be found.";
		const char *source = src.data();
        asizei srcLen = src.size() - 1;
		add.program[pieces] = clCreateProgramWithSource(ctx, 1, &source, &srcLen, &error);
		if(!add.program[pieces] || error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateProgramWithSource.";
		const std::string defines(GetBuildOptions(opt, pieces));
		error = clBuildProgram(add.program[pieces], 1, &use, defines.c_str(), NULL, NULL); // blocking build
		ProbeProgramBuildInfo(add.program[pieces], use, defines);
		if(error != CL_SUCCESS) {
			throw std::string("OpenCL error ") + std::to_string(error) + " returned by clBuildProgram.";
		}

		add.kernel[pieces] = clCreateKernel(add.program[pieces], fikern.second.c_str(), &error);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateKernel while trying to create " + fikern.second.c_str();

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
            if(pieces == 2) {
                cl_uint roundCount = 14;
                error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(roundCount), &roundCount);
                if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg setting SHAvite3 round count.";
            }
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
}
