/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "FreshMultistepOpenCL12.h"

#include <iomanip>

bool FreshMultistepOpenCL12::Dispatch(asizei setIndex, asizei slotIndex) {
#if FRESH_DEBUGBUFFER_SIZE
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
	FreshMultiStep_Options &options(settings[setIndex].options);
	AlgoInstance &use(settings[setIndex].algoInstances[slotIndex]);
    // This could be the same as Qubit, but it really isn't. Instad, I just dispatch everything and wait for a result.
    // It turned out CL dispatch is super fast and I have plenty of CPU power anyway.
    if(use.step < 1) return true; // missing input data
    { // shavite3 head
		const asizei woff[1] = { use.nonceBase };
		const asizei workDim [1] = { options.HashCount() };
		const asizei groupDim[1] = { 64 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[0], 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to fresh[0] kernel.";
	}
    { // SIMD
		const asizei woff[2] = { 0, use.nonceBase };
		const asizei workDim [2] = { 16, options.HashCount() };
		const asizei groupDim[2] = { 16, 4 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[1], 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to fresh[1] kernel.";
	}
    { // shavite3
		const asizei woff[1] = { use.nonceBase };
		const asizei workDim [1] = { options.HashCount() };
		const asizei groupDim[1] = { 64 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[2], 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to fresh[2] kernel.";
	}
    { // SIMD
		const asizei woff[2] = { 0, use.nonceBase };
		const asizei workDim [2] = { 16, options.HashCount() };
		const asizei groupDim[2] = { 16, 4 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[3], 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to fresh[3] kernel.";
	}
    { // echo
		const asizei woff[2] = { 0, use.nonceBase };
		const asizei workDim [2] = { 8, options.HashCount() };
		const asizei groupDim[2] = { 8, 8 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel[4], 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to fresh[4] kernel.";
			
#if FRESH_DEBUGBUFFER_SIZE
		{
			const asizei start = 0;
			const asizei size = 8+32*10+32;
			cl_ulong hostBuff[size];
			error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_ulong), size * sizeof(cl_ulong), hostBuff, 0, 0, 0);
			if(error != CL_SUCCESS) throw std::exception("Something went really wrong in debug buffer map!");

			std::ofstream dbg("debug_FRESH_OUT.txt");
            dbg<<"from prev hash=   ";
            asizei next = 0;
            for(int i = 0; i < 8; i++) dbg<<std::hex<<std::setfill('0')<<std::setw(16)<<hostBuff[next++];
            for(int loop = 0; loop < 10; loop++) {
                dbg<<"\nat end of round "<<loop<<" =      ";
                for(int i = 0; i < 32; i++) dbg<<std::hex<<std::setfill('0')<<std::setw(16)<<hostBuff[next++];
            }
            dbg<<"test= "<<std::hex<<std::setfill('0')<<std::setw(16)<<hostBuff[0];
            dbg<<"\ntarget= "<<std::hex<<std::setfill('0')<<std::setw(16)<<hostBuff[1];
            dbg<<std::endl<<std::endl<<std::endl;
		}
#endif
	}
    {
	    const asizei size = (1 + options.OptimisticNonceCountMatch()) * sizeof(cl_uint);
        cl_int error;
	    void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.nonces, CL_FALSE, CL_MAP_READ, 0, size, 0, NULL, &use.res.resultsTransferred, &error);
	    use.res.mappedNonceBuff = reinterpret_cast<cl_uint*>(raw);
	    if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueMapBuffer while mapping result nonce buffer.";
    }
    use.step = 2;
    return true;
}


void FreshMultistepOpenCL12::HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei resIndex) {
	/* * * * * * * * * * * * */
	//! \todo Implement qubit CPU-side hashing for validation.
	throw std::string("TODO: fresh hash validation not implemented yet. I cannot be bothered with it!");
	/* * * * * * * * * * * * */
}


FreshMultistepOpenCL12::~FreshMultistepOpenCL12() {
	// what if the nonce buffer is still mapped? I destroy both the event and the buffer anyway.
	for(asizei s = 0; s < settings.size(); s++) {
		for(asizei i = 0; i < settings[s].algoInstances.size(); i++) settings[s].algoInstances[i].res.Free();
	}
}


void FreshMultistepOpenCL12::GetSettingsInfo(SettingsInfo &out, asizei setting) const {
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


std::string FreshMultistepOpenCL12::GetBuildOptions(const FreshMultiStep_Options &opt, const auint index) {
	std::stringstream build;
	switch(index) {
	case 0: build<<"-D HEAD_OF_CHAINED_HASHING"; break;
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



std::pair<std::string, std::string> FreshMultistepOpenCL12::GetSourceFileAndKernel(asizei settingIndex, auint step) const {
    using std::string;
    switch(step) {
	case 0: return std::make_pair(string("SHAvite3_1W.cl"), string("SHAvite3_1way"));
	case 1: return std::make_pair(string("SIMD_16W.cl"), string("SIMD_16way"));
	case 2: return std::make_pair(string("SHAvite3_1W.cl"), string("SHAvite3_1way"));
	case 3: return std::make_pair(string("SIMD_16W.cl"), string("SIMD_16way"));
	case 4: return std::make_pair(string("Echo_8W.cl"), string("Echo_8way"));
    }
    string empty;
    return std::make_pair(empty, empty);
}


void FreshMultistepOpenCL12::Parse(FreshMultiStep_Options &opt, const rapidjson::Value &params) {
	const rapidjson::Value::ConstMemberIterator li(params.FindMember("linearIntensity"));
	if(li != params.MemberEnd() && li->value.IsUint() && li->value.GetUint()) opt.linearIntensity = li->value.GetUint();
}


asizei FreshMultistepOpenCL12::ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback) {
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


void FreshMultistepOpenCL12::BuildDeviceResources(FreshMultiStep_Resources &add, cl_context ctx, cl_device_id use, const FreshMultiStep_Options &opt, asizei confIndex) {
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

#if FRESH_DEBUGBUFFER_SIZE
	add.debugBUFFER = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, FRESH_DEBUGBUFFER_SIZE, NULL, &error);
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
        switch(pieces) {
        case 0:
        case 2: { // SHAVite3
            cl_mem target[2][4] = {
                    { add.wuData, add.io[0], add.aesLUTTables, nullptr },
                    { add.io[1],  add.io[0], add.aesLUTTables, nullptr }
            };
            const char *nameString[2][3] = {
                    { "wuData", "io0", "AES tables" },
                    { "io1", "io0", "AES tables" }
            };
			for(asizei loop = 0; target[pieces == 2][loop]; loop++) {
			    input = target[pieces == 2][loop];
				error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(input), &input);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[pieces == 2][loop];
				args++;
			}
            cl_uint roundCount = 14;
            error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(roundCount), &roundCount);
            if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg setting SHAvite3 round count.";
            break;
		}
        case 1:
        case 3: { // SIMD
			cl_mem target[] = { add.io[0], add.io[1], add.io[0], add.alphaTable, add.betaTable, nullptr };
			const char *nameString[] = { "io0-uint", "output uo1", "io0-ubyte", "alpha LUT", "beta LUT" };
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], args, sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
			break;
        }
        case 4: {
			cl_mem target[] = { add.io[1], add.nonces, add.dispatchData, add.aesLUTTables, nullptr };
			const char *nameString[] = { "io1", "nonces", "dispatchData", "AES tables" };
			input = target[0];
			for(asizei loop = 0; target[loop]; loop++) {
				error = clSetKernelArg(add.kernel[pieces], auint(loop), sizeof(target[loop]), target + loop);
				if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
				args++;
			}
            #if FRESH_DEBUGBUFFER_SIZE
                error = clSetKernelArg(add.kernel[pieces], auint(args), sizeof(add.debugBUFFER), &add.debugBUFFER);
		        if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on \"debugBUFFER\"";
            #endif
		}
		}
	}
}
