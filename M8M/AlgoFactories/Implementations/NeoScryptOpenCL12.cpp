/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "NeoScryptMultistepOpenCL12.h"


bool NeoScryptMultistepOpenCL12::Dispatch(asizei setIndex, asizei slotIndex) {
	NeoScryptMultiStep_Options &options(settings[setIndex].options);
	AlgoInstance &use(settings[setIndex].algoInstances[slotIndex]);
    if(use.step < 1) return true; // missing input data
    { // initial kdf, fill buff_a and pull out 256-bit X value to start loops
		const asizei woff[2] = { 0, use.nonceBase };
		const asizei workDim [2] = { 4, options.HashCount() };
		const asizei groupDim[2] = { 4, 16 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.firstKDF, 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(firstKDF)->") + std::to_string(error);
		
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			static bool dumpDebug = true;
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, (256+32+1)*32+256, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt");
				char *hex = "0123456789abcdef";
				auint dump = 0;
				for(auint merda = 31; merda < 32; merda++) {
					dbg<<"\n  loop["<<merda<<"]";
					dbg<<"\n    ";
					for(auint i = 0; i < 256+32;i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
					dbg<<"\n    bufidx="<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				}
				dbg<<"\n  X from 1st KDF=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
			}
		#endif
	}
    { // loop 0: SW, IR (salsa)
		const asizei woff[1] = { use.nonceBase };
		const asizei workDim [1] = { options.HashCount() };
		const asizei groupDim[1] = { 64 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.salsaSW, 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(salsaSW)->") + std::to_string(error);
		
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, 256*2, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt", std::ios::app);
				dbg<<"\n\nSW Salsa in=";
				char *hex = "0123456789abcdef";
				auint dump = 0;
				for(auint merda = 0; merda < 256; merda++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\nSW Salsa out=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
			}
		#endif

		error = clEnqueueNDRangeKernel(use.res.commandq, use.res.salsaIR, 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(salsaIR)->") + std::to_string(error);
		
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			static bool dumpDebug = true;
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, 256*2, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt", std::ios::app);
				dbg<<"\n\nIR Salsa in=";
				char *hex = "0123456789abcdef";
				auint dump = 0;
				for(auint merda = 0; merda < 256; merda++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\nIR Salsa out=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
			}
		#endif
        
		//loop 1: SW, IR (chacha)
		error = clEnqueueNDRangeKernel(use.res.commandq, use.res.chachaSW, 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(chachaSW)->") + std::to_string(error);
		
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, 256*2, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt", std::ios::app);
				dbg<<"\n\nIR Chacha in=";
				char *hex = "0123456789abcdef";
				auint dump = 0;
				for(auint merda = 0; merda < 256; merda++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\nIR Chacha out=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
			}
		#endif
		error = clEnqueueNDRangeKernel(use.res.commandq, use.res.chachaIR, 1, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(chachaIR)->") + std::to_string(error);
		
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, 256*2, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt", std::ios::app);
				dbg<<"\n\nIR Chacha in=";
				char *hex = "0123456789abcdef";
				auint dump = 0;
				for(auint merda = 0; merda < 256; merda++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\nIR Chacha out=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
			}
		#endif
	}
    { // final KDF and nonce filter
		const asizei woff[2] = { 0, use.nonceBase };
		const asizei workDim [2] = { 4, options.HashCount() };
		const asizei groupDim[2] = { 4, 16 };
		cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.lastKDF, 2, woff, workDim, groupDim, 0, NULL, NULL);
		if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(lastKDF)->") + std::to_string(error);

		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			static bool dumpDebug = true;
			if(dumpDebug) {
				void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, CL_MAP_READ, 0, 80+256, 0, NULL, NULL, NULL);
				const unsigned char *val = reinterpret_cast<const unsigned char*>(raw);
				std::ofstream dbg("NS_CL_DBG.txt", std::ios::app);
				dbg<<"\n";
				char *hex = "0123456789abcdef";
				auint dump = 0;
				dbg<<"\n  X to last KDF=";
				for(int i = 0; i < 256; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\n  hash out=";
				for(int i = 0; i < 32; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\n  outbuf[7]=";
				for(int i = 0; i < 1*4; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];
				dbg<<"\n  target=";
				for(int i = 0; i < 1*4; i++) dbg<<hex[val[dump] >> 4]<<hex[val[dump++] & 0x0F];

				clEnqueueUnmapMemObject(use.res.commandq, use.res.debugBUFFER, raw, 0, NULL, NULL);
				dumpDebug = false;
			}
		#endif
	}
	const asizei size = (1 + options.OptimisticNonceCountMatch()) * sizeof(cl_uint);
    cl_int error = 0;
	void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.nonces, CL_FALSE, CL_MAP_READ, 0, size, 0, NULL, &use.res.resultsTransferred, &error);
	use.res.mappedNonceBuff = reinterpret_cast<cl_uint*>(raw);
	if(error != CL_SUCCESS) throw std::string("clEnqueueNDRangeKernel(lastKDF)->") + std::to_string(error);
    use.step = 2;

    return true;
}


void NeoScryptMultistepOpenCL12::HashHeader(std::array<aubyte, 32> &hash, const std::array<aubyte, 128> &header, asizei setIndex, asizei resIndex) {
	/* * * * * * * * * * * * */
	//! \todo Implement neoscrypt CPU-side hashing for validation.
	throw std::string("TODO: neoscrypt hash validation not implemented yet. I cannot be bothered with it!");
	/* * * * * * * * * * * * */
}


NeoScryptMultistepOpenCL12::~NeoScryptMultistepOpenCL12() {
	// what if the nonce buffer is still mapped? I destroy both the event and the buffer anyway.
	for(asizei s = 0; s < settings.size(); s++) {
		for(asizei i = 0; i < settings[s].algoInstances.size(); i++) settings[s].algoInstances[i].res.Free();
	}
}


void NeoScryptMultistepOpenCL12::GetSettingsInfo(SettingsInfo &out, asizei setting) const {
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
	out.Push(deviceRes, constQual, "dispatch data", 5 * sizeof(cl_uint));
	out.Push(deviceRes, inoutQual, "KDF buffer \"A\"", (256 + 64) * opt.HashCount());
	out.Push(deviceRes, inoutQual, "KDF buffer \"B\"", (256 + 32) * opt.HashCount());
	out.Push(deviceRes, inoutQual, "KDF result", 256 + 32 * opt.HashCount());
	out.Push(deviceRes, inoutQual, "Scratchpad values",  32 * 1024 * opt.HashCount());
	out.Push(deviceRes, inoutQual, "loop y<sub>0</sub> X value", 256 * opt.HashCount());
	out.Push(deviceRes, inoutQual, "loop y<sub>1</sub> X value", 256 * opt.HashCount());
	out.Push(hostRes,   outQual,   "found nonces",    sizeof(cl_uint) * (1 + opt.OptimisticNonceCountMatch()));
}


std::string NeoScryptMultistepOpenCL12::GetBuildOptions(const NeoScryptMultiStep_Options &opt, const auint index) {
	std::stringstream build;
	switch(index) {
	case 0: break;
	case 1: build<<"-D BLOCKMIX_SALSA"; break;
	case 2: build<<"-D BLOCKMIX_SALSA"; break;
	case 3: build<<"-D BLOCKMIX_CHACHA"; break;
	case 4: build<<"-D BLOCKMIX_CHACHA"; break;
	case 5: break;
	}
#if defined(_DEBUG)
	//build<<" -g"; //cl2.0 only, even though CodeXL wants it!
#endif
	// CL2.0 //
	//build<<" -cl-uniform-work-group-size";
	return build.str();
}



std::pair<std::string, std::string> NeoScryptMultistepOpenCL12::GetSourceFileAndKernel(asizei settingIndex, auint step) const {
    using std::string;
    switch(step) {
	case 0: return std::make_pair(string("ns_KDF_4W.cl"), string("firstKDF_4way"));
	case 1: return std::make_pair(string("ns_coreLoop_1W.cl"), string("sequentialWrite_1way"));
	case 2: return std::make_pair(string("ns_coreLoop_1W.cl"), string("indirectedRead_1way"));
	case 3: return std::make_pair(string("ns_coreLoop_1W.cl"), string("sequentialWrite_1way"));
	case 4: return std::make_pair(string("ns_coreLoop_1W.cl"), string("indirectedRead_1way"));
	case 5: return std::make_pair(string("ns_KDF_4W.cl"), string("lastKDF_4way"));
    }
    string empty;
    return std::make_pair(empty, empty);
}


void NeoScryptMultistepOpenCL12::Parse(NeoScryptMultiStep_Options &opt, const rapidjson::Value &params) {
	const rapidjson::Value::ConstMemberIterator li(params.FindMember("linearIntensity"));
	if(li != params.MemberEnd() && li->value.IsUint() && li->value.GetUint()) opt.linearIntensity = li->value.GetUint();
}


asizei NeoScryptMultistepOpenCL12::ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback) {
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

    // Evaluate max concurrency. Big problem here is the 32KiB scratchpad.
	const asizei buffBytes = settings[0].options.HashCount() * 32 * 1024;
    const aulong maxBuffBytes = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::diul_maxMemAlloc);
    if(buffBytes > maxBuffBytes) {
        std::string err("intensity too high, max buffer size is ");
        err += std::to_string(maxBuffBytes) + " Bytes";
        badthing(err.c_str());
    }

	if(fail) return settings.size();

	return 0; // ok if we support a single config.
}


void NeoScryptMultistepOpenCL12::BuildDeviceResources(NeoScryptMultiStep_Resources &add, cl_context ctx, cl_device_id use, const NeoScryptMultiStep_Options &opt, asizei confIndex) {
   
	//! \todo Ok, guess what? All algos will always have a command queue and a dispatch data buffer, as well as a block header buffer.
	//! It would be about time to start integrate those things in an unique place.
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
	add.nonces = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, (1 + opt.OptimisticNonceCountMatch()) * sizeof(cl_uint), NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting nonces buffer.";

#if NEOSCRYPT_DEBUGBUFFER_SIZE
	add.debugBUFFER = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, NEOSCRYPT_DEBUGBUFFER_SIZE, NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting debug buffer.";
#endif
	
	byteCount = (256 + 64) * opt.HashCount();
	add.buff_a = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create buff_a.";
	byteCount = (256 + 32) * opt.HashCount();
	add.buff_b = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create buff_b.";
	byteCount = 256 * opt.HashCount();
	add.xstart = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create xstart.";
	byteCount = 32 * 1024 * opt.HashCount();
	add.scratchPad = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create scratchPad.";
	byteCount = 256 * opt.HashCount();
	add.x0 = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create loop [0] result.";
	byteCount = 256 * opt.HashCount();
	add.x1 = clCreateBuffer(ctx, CL_MEM_HOST_NO_ACCESS, byteCount, NULL, &error);
	if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create loop [1] result.";

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

		cl_kernel kernel = clCreateKernel(add.program[pieces], fikern.second.c_str(), &error);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateKernel while trying to create " + fikern.second.c_str();
		switch(pieces) {
		case 0: add.firstKDF = kernel; break;
		case 1: add.salsaSW = kernel; break;
		case 2: add.salsaIR = kernel; break;
		case 3: add.chachaSW = kernel; break;
		case 4: add.chachaIR = kernel; break;
		case 5: add.lastKDF = kernel; break;
		}

		cl_uint args = 0;
		const cl_uint CONST_N = 32;
		const cl_uint NS_ITERATIONS = 128;
		switch(pieces) {
		case 0: { // first kdf
			clSetKernelArg(kernel, args++, sizeof(add.wuData), &add.wuData);
			clSetKernelArg(kernel, args++, sizeof(add.xstart), &add.xstart);
			clSetKernelArg(kernel, args++, sizeof(CONST_N), &CONST_N);
			clSetKernelArg(kernel, args++, sizeof(add.buff_a), &add.buff_a);
			clSetKernelArg(kernel, args++, sizeof(add.buff_b), &add.buff_b);
			break;
		}
		case 1:
		case 3: {
			cl_mem target = pieces == 1? add.x0 : add.x1;
			clSetKernelArg(kernel, args++, sizeof(add.xstart), &add.xstart);
			clSetKernelArg(kernel, args++, sizeof(add.scratchPad), &add.scratchPad);
			clSetKernelArg(kernel, args++, sizeof(ITERATIONS), &ITERATIONS);
			clSetKernelArg(kernel, args++, sizeof(STATE_SLICES), &STATE_SLICES);
			clSetKernelArg(kernel, args++, sizeof(MIX_ROUNDS), &MIX_ROUNDS);
			clSetKernelArg(kernel, args++, sizeof(target), &target);
			break;
		}
		case 2:
		case 4: {
			cl_mem target = pieces == 2? add.x0 : add.x1;
			clSetKernelArg(kernel, args++, sizeof(target), &target);
			clSetKernelArg(kernel, args++, sizeof(add.scratchPad), &add.scratchPad);
			clSetKernelArg(kernel, args++, sizeof(NS_ITERATIONS), &NS_ITERATIONS);
			clSetKernelArg(kernel, args++, sizeof(STATE_SLICES), &STATE_SLICES);
			clSetKernelArg(kernel, args++, sizeof(MIX_ROUNDS), &MIX_ROUNDS);
			break;
		}
		case 5: {
			clSetKernelArg(kernel, args++, sizeof(add.nonces), &add.nonces);
			clSetKernelArg(kernel, args++, sizeof(add.dispatchData), &add.dispatchData);
			clSetKernelArg(kernel, args++, sizeof(add.x0), &add.x0);
			clSetKernelArg(kernel, args++, sizeof(add.x1), &add.x1);
			clSetKernelArg(kernel, args++, sizeof(CONST_N), &CONST_N);
			clSetKernelArg(kernel, args++, sizeof(add.buff_a), &add.buff_a);
			clSetKernelArg(kernel, args++, sizeof(add.buff_b), &add.buff_b);
			clSetKernelArg(kernel, args++, sizeof(add.scratchPad), &add.scratchPad);
			break;
		}
		}
		#if NEOSCRYPT_DEBUGBUFFER_SIZE
			clSetKernelArg(kernel, args++, sizeof(add.debugBUFFER), &add.debugBUFFER);
		#endif
	}
}
