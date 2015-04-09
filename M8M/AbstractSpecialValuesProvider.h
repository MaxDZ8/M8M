/*
 * Copyright (C) 2015 Massimo Del Zotto
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <string>
#include <CL/cl.h>

/*! Some values are special as they need to be bound on a per-dispatch basis... in certain cases.
This structure allows this base class to understand if a value is "special". Do not confuse those with "known constant" values, which are special
but in a different way. They are, by definition, constant and thus not "special" in this sense. */
struct SpecialValueBinding {
    bool earlyBound; //!< true if this parameter can be bound to the kernel once and left alone forever. Otherwise, call DispatchBinding.
    union {
        cl_mem buff;   //!< buffer holding the specific resource. Use when earlyBound.
        cl_uint index; //!< resource index (to be mapped to a specific buffer) for dispatch-time binding.
    } resource;
};


/*! This structure is used by AbstractSpecialValuesProvider to tell the algorithm new cl_mem values and when a value has been updated. */
struct LateBinding {
    cl_mem buff;
    bool rebind;
    LateBinding() { buff = 0;    rebind = true; }
};


/*! Algorithms are fairly dumb little things. They basically specify a set of kernels to be called in sequence and how to bind parameters to them.
Not all parameters however are created equal. Some values never change and can be bound easily, some others change every time the algo is dispatched.
To cater with this second need an object implementing this interface is used.

The bottom line is: when a kernel initializes, it looks out for "special parameters". If a special parameter with the given name exists and is to be
re-bound at each dispatch then the kernel provides the KernelParamsProviderIntrface object a cl_mem pointer. The outer machinery can change the cl_mem
value here and expect them to be rebound at next dispatch.

\note If the contents of the buffer changes but the buffer stays the same then you don't need this trick!
\note Resist in providing anything more than cl_mem: in the future, everything should go through a single cbuffer.
   Immediate params are for convenience only. A system to merge multiple immediates to cbuffer shall be deployed instead of extending this.
\note In the case of future multi-phased algorithms, special care shall be taken to either cancel processing or at least keep the values
   coherent for a whole iteration! */
class AbstractSpecialValuesProvider {
public:
    virtual ~AbstractSpecialValuesProvider() { }
    
    /*! This function returns false if the given name does not identify something recognized.
    Otherwise, it will set desc. You can be sure at this point desc.resource will always be != 0.
    While there are no requirements on the special value names, please follow these guidelines:
    - All special value names start with '$'.
    - "$wuData" is the 80-bytes block header to hash. Yes, 80 bytes, even though we overwrite the last 4 (most of the time).
    - "$dispatchData" contains "other stuff" including targetbits... note those are probably going to be refactored as well.
    - "$candidates" is the resulting nonce buffer.
    Those can be bound early or dinamically, there's no requirement. */
    bool SpecialValue(SpecialValueBinding &desc, const std::string &name) const {
        for(auto test : specials) {
            if(test.name == name) {
                desc = test.binding;
                return true;
            }
        }
        return false;
    }

    bool SpecialValue(const std::string &name) const {
        SpecialValueBinding dummy;
        return SpecialValue(dummy, name);
    }

    /*! An algorithm "registers" for special value fetching by providing a slot of memory where the AbstractSpecialValuesProvider object
    can push buffer changes. This prediliges Algorithm fast access as kernel dispatching is assumed higher frequency.
    Note asking to push a early-bound buffer is undefined, as they don't have a valueIndex! */
    virtual void Push(LateBinding &slot, asizei valueIndex) = 0;

protected:
    struct NamedValue {
        std::string name;
        SpecialValueBinding binding;
        explicit NamedValue() = default;
        NamedValue(const char *n, const SpecialValueBinding &b) : name(n), binding(b) { }
    };

    //! List of my special values. In theory I would make this private but there's really little point. Just have ctors push them.
    std::vector<NamedValue> specials;
};
