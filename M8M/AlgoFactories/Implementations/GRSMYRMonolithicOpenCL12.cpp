/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "grsmyrMonolithicOpenCL12.h"

bool GRSMYRMonolithicOpenCL12::Dispatch(asizei setIndex, asizei slotIndex) {
	GRSMYRMonolithic_Options &options(settings[setIndex].options);
	AlgoInstance &use(settings[setIndex].algoInstances[slotIndex]);
    if(use.step == 1) {
	    const asizei woff[1] = { use.nonceBase };
	    const asizei workDim [1] = { options.HashCount() };
	    const asizei groupDim[1] = { 256 }; // based on observation from profiling slightly better.
	    cl_int error = clEnqueueNDRangeKernel(use.res.commandq, use.res.kernel, 1, woff, workDim, groupDim, 0, NULL, NULL);
	    if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueNDRangeKernel while writing to grsmyr-monolithic kernel.";			
    
        const asizei size = (1 + options.OptimisticNonceCountMatch()) * sizeof(cl_uint);
	    void *raw = clEnqueueMapBuffer(use.res.commandq, use.res.nonces, CL_FALSE, CL_MAP_READ, 0, size, 0, NULL, &use.res.resultsTransferred, &error);
	    use.res.mappedNonceBuff = reinterpret_cast<cl_uint*>(raw);
	    if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clEnqueueMapBuffer while mapping result nonce buffer.";
        use.step++;
        
#if GROESTLMYR_DEBUGBUFFER_SIZE
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

		{
			const asizei start = 0;
			const asizei size = 8*7+8*5+2+2;
			cl_uint hostBuff[size];
			error = clEnqueueReadBuffer(use.res.commandq, use.res.debugBUFFER, CL_TRUE, start * sizeof(cl_uint), size * sizeof(cl_uint), hostBuff, 0, 0, 0);
			if(error != CL_SUCCESS) { return true; }

			unsigned char *bytes = reinterpret_cast<unsigned char*>(hostBuff);
			{
				std::ofstream dbg("debug_GRSMYR_OUT.txt", std::ios::app);
				for(int i = 0; i < 7; i++) {
                    dbg<<"\nhalf const round= ";
				    bytes = UintHexOut(dbg, bytes, 8, false);
                }
				dbg<<"\nv= ";
				bytes = UintHexOut(dbg, bytes, 8, false);
				dbg<<"\ns= ";
				bytes = UintHexOut(dbg, bytes, 8, false);
				dbg<<"\nv+s= ";
				bytes = UintHexOut(dbg, bytes, 8, false);
				dbg<<"\nswap(v+s)= ";
				bytes = UintHexOut(dbg, bytes, 8, false);
				dbg<<"\nsha256= ";
				bytes = UintHexOut(dbg, bytes, 8, false);
				dbg<<"\ntest= ";
				bytes = UintHexOut(dbg, bytes, 2, false);
				dbg<<"\ntarget= ";
				bytes = UintHexOut(dbg, bytes, 2, false);
				dbg<<std::endl<<std::endl<<std::endl<<std::endl;
			}
		}
#endif
    }
    return true;
}


void GRSMYRMonolithicOpenCL12::GetSettingsInfo(SettingsInfo &out, asizei setting) const {
	// In line of theory I could design some fairly contrived way to do everything using GenResources...
	// BUT I would end mixing logic and presentation code. Not a good idea. Since algo implementations don't change very often,
	// I think it's a better idea to just keep them in sync.
	const auto &opt(settings[setting].options);
	out.hashCount = opt.HashCount();
	std::vector<std::string> constQual, inoutQual, outQual;
	constQual.push_back(std::string("const"));
	outQual.push_back(std::string("out"));

	const std::string deviceRes("device");
	const std::string hostRes("host");

	out.Push(deviceRes, constQual, "work unit data",   128);
	out.Push(deviceRes, constQual, "dispatch info",    5 * sizeof(cl_uint));
	out.Push(deviceRes, constQual, "roundCount", 5 * sizeof(cl_uint));
	out.Push(hostRes,   outQual,   "found nonces ",    sizeof(cl_uint) * (1 + opt.OptimisticNonceCountMatch()));
}


std::string GRSMYRMonolithicOpenCL12::GetBuildOptions(const GRSMYRMonolithic_Options &opt, const auint index) const {
    // -D worksize?
    return "";
}


std::pair<std::string, std::string> GRSMYRMonolithicOpenCL12::GetSourceFileAndKernel(asizei settingIndex, auint stepIndex) const {
    std::string empty;
    if(stepIndex) return std::make_pair(empty, empty);
    return std::make_pair(std::string("grsmyr_monolithic.cl"), std::string("grsmyr_monolithic"));
}


void GRSMYRMonolithicOpenCL12::Parse(GRSMYRMonolithic_Options &opt, const rapidjson::Value &params) {
	const rapidjson::Value::ConstMemberIterator li(params.FindMember("linearIntensity"));
	if (li != params.MemberEnd() && li->value.IsUint() && li->value.GetUint()) opt.linearIntensity = li->value.GetUint();
}


asizei GRSMYRMonolithicOpenCL12::ChooseSettings(const OpenCL12Wrapper::Platform &plat, const OpenCL12Wrapper::Device &dev, RejectReasonFunc callback) {
	//! \todo support multiple configurations.
	//! \todo copied and slightly modified from QubitMultistepOpenCL12... should I unificate those somehow?
	//! \todo copypaste!
	bool fail = false;
	auto badthing = [&callback, &fail](const char *str) { fail = true;    if (callback) callback(str); };

	if (OpenCL12Wrapper::GetDeviceGroupInfo(plat, OpenCL12Wrapper::dgis_profile) != "FULL_PROFILE") badthing("full profile required");
	if (dev.type.gpu == false) badthing("must be GPU");

	const std::string extensions = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::dis_extensions);
	const auint vid = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::diu_vendorID);
	const std::string chip = OpenCL12Wrapper::GetDeviceInfo(dev, OpenCL12Wrapper::dis_chip);
	auto chipType = dev.type.gpu? KnownHardware::ct_gpu : KnownHardware::ct_cpu;
	KnownHardware::Architecture arch = KnownHardware::GetArchitecture(vid, chip.c_str(), chipType, extensions.c_str());
	if (arch < KnownHardware::arch_gcn_first || arch > KnownHardware::arch_gcn_last) badthing("must be AMD GCN");

	// Qubit limits concurrency to match a certain buffer size. Monolithic kernels don't have this limitation.

	if (fail) return settings.size();

	return 0; // ok if we support a single config.
}


void GRSMYRMonolithicOpenCL12::BuildDeviceResources(GRSMYRMonolithic_Resources &add, cl_context ctx, cl_device_id use, const GRSMYRMonolithic_Options &opt, asizei confIndex) {
	//! \todo yet more copy-paste from qubit...
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

    {
        auint counts[5] = { 
            14, 14, 14, // groestl rounds
            2, 3 // SHA rounds
        };
        byteCount = sizeof(counts);
        add.roundCount = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_HOST_NO_ACCESS | CL_MEM_COPY_HOST_PTR, byteCount, counts, &error);
	    if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to create roundCount buffer.";
    }

#if GROESTLMYR_DEBUGBUFFER_SIZE
	add.debugBUFFER = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, GROESTLMYR_DEBUGBUFFER_SIZE, NULL, &error);
	if(error) throw std::string("OpenCL error ") + std::to_string(error) + " while trying to resulting debug buffer.";
#endif

	//! \todo this is absolutely trivial in the base class as well! I should refactor this part...
	//! loading/compiling is particularly easy and can truly be slapped in a base class with no particular hassle...
	//! with some work even binding is not much of a problem, but due to multi-device stuff it's probably worth changing this in a two-phase
	//! architecture.
    std::vector<char> src;
	//! \todo in a multi-device situation we would really be reloading the same stuff from disk multiple times. I really must build a temporary cache for source!
	{
		auto fikern = GetSourceFileAndKernel(confIndex, 0);
		fikern.first = "kernels/" + fikern.first;
		if(!GetSourceFromFile(src, fikern.first)) throw std::string("OpenCL code must be loaded and compiled explicitly, but \"") + fikern.first + "\" cannot be found.";
		const char *source = src.data();
        asizei srcLen = src.size() - 1;
		add.program = clCreateProgramWithSource(ctx, 1, &source, &srcLen, &error);
		if(!add.program || error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateProgramWithSource.";
		const std::string defines(GetBuildOptions(opt, 0));
		error = clBuildProgram(add.program, 1, &use, defines.c_str(), NULL, NULL); // blocking build
		ProbeProgramBuildInfo(add.program, use, defines);
		if(error != CL_SUCCESS) {
			throw std::string("OpenCL error ") + std::to_string(error) + " returned by clBuildProgram.";
		}

		add.kernel = clCreateKernel(add.program, fikern.second.c_str(), &error);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clCreateKernel while trying to create " + fikern.second;

		cl_uint args = 0;
		cl_mem input;
		cl_mem target[] = { add.nonces, add.wuData, add.dispatchData, add.roundCount };
        const char *nameString[] = { "nonces", "wuData", "dispatchData", "roundCount", nullptr };
		input = target[0];
		for(asizei loop = 0; nameString[loop]; loop++) {
			error = clSetKernelArg(add.kernel, auint(loop), sizeof(target[loop]), target + loop);
			if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on " + nameString[loop];
			args++;
		}
#if GROESTLMYR_DEBUGBUFFER_SIZE
		error = clSetKernelArg(add.kernel, auint(args), sizeof(add.debugBUFFER), &add.debugBUFFER);
		if(error != CL_SUCCESS) throw std::string("OpenCL error ") + std::to_string(error) + " returned by clSetKernelArg on \"debugBUFFER\"";
#endif
	}
}
