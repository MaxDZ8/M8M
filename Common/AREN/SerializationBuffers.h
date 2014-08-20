/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/*! \file This is part of AREN but modified for use in M8M,
some stuff has been pulled out (such as Multi-byte int and Half support,) while something else
has been put in (the better HTON metod). */
#pragma once
#include "ArenDataTypes.h"


template<typename scalar>
scalar HTON(const scalar v) {
#if defined(_M_AMD64) || defined _M_IX86
	scalar ret;
	__int8 *dst = reinterpret_cast<__int8*>(&ret);
	const __int8 *src = reinterpret_cast<const __int8*>(&v) + sizeof(v) - 1;
	for(size_t cp = 0; cp < sizeof(scalar); cp++) {
		*dst = *src;
		dst++;
		src--;
	}
	return ret;
#else
#error HTON requires some attention!
#endif
}


template<typename scalar>
scalar HTOLE(const scalar v) {
#if defined(_M_AMD64) || defined _M_IX86
	return v;
#else
#error HTOLE requires some attention!
#endif
}


template<typename scalar>
scalar LETOH(const scalar v) {
#if defined(_M_AMD64) || defined _M_IX86
	return v;
#else
#error LETOH requires some attention!
#endif
}

//namespace sharedUtils {

// gone, I need serialization only in M8M
//class SourceStream { ... };


class DestinationStream {
	const asizei byteCount;
	aubyte* const blob;
	asizei consumed;

private:
	DestinationStream(const DestinationStream &other); //!< private plus missing LINK, don't copy.

	template<class PODType>
	void WriteImplementation(PODType store) {
		if(byteCount - consumed < sizeof(store)) throw new std::exception("buffer too small, cannot write required byte count."); 
		store = HTON(store); // Differently from AREN, this saves in NETWORK BIG ENDIAN ORDER, not in X86 order.
		memcpy_s(blob + consumed, byteCount - consumed, &store, sizeof(store));
		consumed += sizeof(store);
	}

public:
	DestinationStream(aubyte* arr, const asizei count) : byteCount(count), blob(arr), consumed(0) { }
	DestinationStream(aushort* arr, const asizei count) : byteCount(count * 2), blob(reinterpret_cast<aubyte*>(arr)), consumed(0) { }
	DestinationStream(auint* arr, const asizei count) : byteCount(count * 4), blob(reinterpret_cast<aubyte*>(arr)), consumed(0) { }
	asizei Consumed() const { return consumed; }
	
	// Stuff here could be templated. But I won't, I want to allow this only for specific types. This is thereby a quite boring file.
	// AREN has explicit Write calls. They resisted as I rarely touch serialization anymore, but this is really going differently.
	DestinationStream& operator<<(const aubyte &store)  { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const aushort &store) { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const auint &store)   { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const aulong &store)  { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const abyte &store)   { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const ashort &store)  { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const aint &store)    { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const along &store)   { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const asingle &store) { WriteImplementation(store);    return *this; }
	DestinationStream& operator<<(const adouble &store) { WriteImplementation(store);    return *this; }
	// New for M8M: basic support for arrays. Note AREN won't have a big deal with them: it uses multibyte encoding
	// just too often.
	template<typename POD, asizei SZ>
	DestinationStream& operator<<(const std::array<POD, SZ> &store) {
		for(asizei loop = 0; loop < store.size(); loop++) (*this)<<store[loop];
		return *this;
	}
	template<typename POD>
	DestinationStream& operator<<(const std::vector<POD> &store) {
		for(asizei loop = 0; loop < store.size(); loop++) (*this)<<store[loop];
		return *this;
	}

	// Those are called WriteArr in AREN to avoid conflicting with Write, which is now operator<< so we're fine!
	// Those can also now be templated as the type check will have to be resolved on operator<< anyway
	template<typename POD>
	DestinationStream& Write(const POD *store, asizei count) {
		for(asizei loop = 0; loop < count; loop++) (*this)<<store[loop];
		return *this;
	}

	/* Interface with old array-based serialization. Those two functions look ugly as they are really meant to be sparingly used. */
	void operator+=(asizei move) { consumed += move; }
};

//}
