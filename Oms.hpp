#pragma once

/*
 * Copyright 2026 L. Richard Moore Jr.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file Oms.hpp
 * @brief One More Serializer — a single-header C++20 binary serialization library.
 *
 * OMS stores typed data in a hierarchical key-value model (Structure, Array) and
 * serializes it to a compact little-endian binary format. A Section wrapper adds a
 * GUID-tagged framing layer that supports random access and streaming data sets that
 * exceed available memory.
 *
 * **Typical write pattern:**
 * @code
 * oms::Section section;
 * section.name = "config";
 * section.add("version", std::uint32_t{1});
 * section.add("label", std::string{"hello"});
 * std::ofstream out("data.oms", std::ios::binary);
 * out << section;                          // writes and clears the section
 * @endcode
 *
 * **Typical read pattern:**
 * @code
 * std::ifstream in("data.oms", std::ios::binary);
 * oms::Section section;
 * in >> section;
 * std::uint32_t ver = section.getOr("version", std::uint32_t{0});
 * @endcode
 */

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <format>
#include <charconv>
#include <cstring>
#include <type_traits>
#if __has_include("commons/Ensure.hpp")
#  include "commons/Ensure.hpp"
#elif __has_include("Ensure.hpp")
#  include "Ensure.hpp"
#else
#  include <cassert>
#  include <utility>
#  if !defined(COMMONS_ENSURE_HPP)
     template<class T, class... Args>
     constexpr inline void throw_if(bool condition, Args&&... args) {
         if(condition) throw T(std::forward<Args>(args)...);
     }
#    define ensure(condition, ...) assert((condition))
#    ifndef NDEBUG
#      include <iostream>
#      define caution(msg) (std::cerr << "Caution: " << (msg) << '\n')
#    else
#      define caution(...) ((void)0)
#    endif
#  endif
#endif

namespace oms {

	static_assert(sizeof(float) == 4);
	static_assert(sizeof(double) == 8);
	static_assert(sizeof(long long) == 8);
	static_assert(sizeof(size_t) <= 8);

#ifndef GUID1
#define GUID1		(0x77b8ae52c9444547ULL)
#endif
#ifndef GUID2
#define GUID2		(0xb0d6366963b2cb31ULL)
#endif
#ifndef MemberCount
	using MemberCount = std::uint16_t;
#endif
#ifndef ItemCount
	using ItemCount = std::uint32_t;
#endif

	class Structure;
	class Array;
	class Variant;

	/**
	 * @brief Runtime type tag stored in every OMS value's binary representation.
	 *
	 * The underlying byte value is written to the wire and must never be renumbered.
	 * @c unsupported (0) is the sentinel for an uninitialised or unrecognised Variant.
	 * @c _last is a range guard and is not a real data type.
	 */
	enum class DataType : std::uint8_t {
		unsupported = 0,  ///< Uninitialised or unrecognised type
		structure   = 1,  ///< oms::Structure
		array       = 2,  ///< oms::Array
		string      = 3,  ///< oms::String (null-terminated UTF-8)
		uint8       = 4,  ///< Primitive<uint8_t>
		uint16      = 5,  ///< Primitive<uint16_t>
		uint32      = 6,  ///< Primitive<uint32_t>
		uint64      = 7,  ///< Primitive<uint64_t> (also used for size_t on 64-bit platforms)
		int8        = 8,  ///< Primitive<int8_t> / char
		int16       = 9,  ///< Primitive<int16_t>
		int32       = 10, ///< Primitive<int32_t>
		int64       = 11, ///< Primitive<int64_t>
		float4      = 12, ///< Primitive<float> (IEEE 754 single-precision)
		float8      = 13, ///< Primitive<double> (IEEE 754 double-precision)
		boolean     = 14, ///< Primitive<bool>
		blob        = 15, ///< oms::Blob (raw byte buffer)
		uint8v      = 16, ///< Vector<uint8_t>
		uint16v     = 17, ///< Vector<uint16_t>
		uint32v     = 18, ///< Vector<uint32_t>
		uint64v     = 19, ///< Vector<uint64_t>
		int8v       = 20, ///< Vector<int8_t>
		int16v      = 21, ///< Vector<int16_t>
		int32v      = 22, ///< Vector<int32_t>
		int64v      = 23, ///< Vector<int64_t>
		float4v     = 24, ///< Vector<float>
		float8v     = 25, ///< Vector<double>
		_last       = 26  ///< Sentinel — not a real type
	};

	/**
	 * @brief Heap-allocates a default-constructed Variant of the given type.
	 *
	 * The caller takes ownership of the returned pointer. Prefer the type-safe
	 * @c add / @c addStructure / @c addArray / @c addVector helpers on Structure when
	 * the type is known at compile time; use this function when the type is determined
	 * at runtime (e.g. during custom deserialization).
	 *
	 * @param type The DataType to instantiate.
	 * @throws std::bad_typeid if @p type is @c DataType::unsupported or @c DataType::_last.
	 */
	constexpr static inline Variant* create(DataType type);

	/**
	 * @brief Polymorphic base for every OMS value.
	 *
	 * Concrete types — @c Primitive<T>, @c String, @c Blob, @c Vector<T>,
	 * @c Structure, @c Array — all inherit from Variant. Callers typically
	 * interact through Structure's typed accessors and receive @c Variant
	 * references, which can be cast to the concrete type via the conversion
	 * operators or by calling @c getType() and casting the reference directly.
	 *
	 * Serialization is via @c operator<< / @c operator>>; do not call
	 * @c write() / @c read() directly.
	 *
	 * @note There is no @c operator const char* conversion. Returning a pointer
	 *       into a String's internal buffer would dangle when the String is a
	 *       temporary, and would silently break the @c operator==(const T&) template
	 *       (the template compares as pointers, not as strings). Use
	 *       @c operator std::string() instead, or compare with string literals:
	 *       @code
	 *       using namespace std::string_literals;
	 *       bool match = (variant == "hello"s);
	 *       @endcode
	 */
	class Variant {
	public:
		virtual ~Variant(){};

		/** @brief Returns the runtime DataType tag of this value. */
		virtual DataType getType() const { return DataType::unsupported; }

		/**
		 * @brief Implicit conversion to a scalar type.
		 *
		 * Numeric overloads apply the same promotion and truncation rules as
		 * ordinary C++ casts. @c operator std::string() formats the stored value
		 * as text: booleans yield @c "true" / @c "false"; char yields a
		 * single-character string; numbers use the shortest exact representation.
		 * Any conversion not supported by the concrete type throws @c std::bad_cast.
		 *
		 * @throws std::bad_cast if the concrete type does not support this conversion.
		 */
		virtual operator std::string() const { throw std::bad_cast(); }
		// On LP64 Linux and LLP64 Windows, size_t and uint64_t are the same underlying
		// type (both unsigned long or both unsigned long long), so declaring both here
		// would be a duplicate and GCC rejects it. On Apple platforms (macOS/iOS),
		// uint64_t is unsigned long long while size_t is unsigned long — genuinely
		// distinct types that both need an operator. Same applies to 32-bit platforms
		// where size_t is 32-bit but uint64_t is still 64-bit.
#if defined(__APPLE__) || (defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ < 8)
		virtual operator size_t() const { throw std::bad_cast(); }
#endif
		virtual operator char() const { throw std::bad_cast(); }
		virtual operator std::uint8_t() const { throw std::bad_cast(); }
		virtual operator std::uint16_t() const { throw std::bad_cast(); }
		virtual operator std::uint32_t() const { throw std::bad_cast(); }
		virtual operator std::uint64_t() const { throw std::bad_cast(); }
		virtual operator std::int8_t() const { throw std::bad_cast(); }
		virtual operator std::int16_t() const { throw std::bad_cast(); }
		virtual operator std::int32_t() const { throw std::bad_cast(); }
		virtual operator std::int64_t() const { throw std::bad_cast(); }
		virtual operator float() const { throw std::bad_cast(); }
		virtual operator double() const { throw std::bad_cast(); }
		virtual operator bool() const { throw std::bad_cast(); }

		/**
		 * @brief Returns a raw pointer to the internal data buffer (Blob and Vector only).
		 *
		 * The pointer is valid for the lifetime of this object and must not be deleted.
		 * Meaningful only for @c Blob and @c Vector<T>; all other types throw.
		 *
		 * @throws std::bad_cast if this is not a Blob or Vector.
		 */
		virtual operator void*() const { throw std::bad_cast(); }

		/**
		 * @brief Key-based subscript into a Structure.
		 *
		 * Meaningful only for Structure; all other concrete types throw.
		 *
		 * @throws std::out_of_range if this is not a Structure or the key is absent.
		 */
	    virtual const Variant& operator[](const std::string& key) const { return operator[](key.c_str()); }
	    virtual const Variant& operator[](const char* ) const { throw std::out_of_range("not subscriptable"); }

		/**
		 * @brief Number of elements in this value.
		 *
		 * Semantics vary by concrete type:
		 * | Type          | Meaning                                   |
		 * |---------------|-------------------------------------------|
		 * | Structure     | number of members                         |
		 * | Array         | number of elements                        |
		 * | Vector\<T\>   | number of elements                        |
		 * | String        | character count (excluding null terminator)|
		 * | Blob          | byte count                                |
		 * | Primitive\<T\>| @c sizeof(T)                              |
		 */
		virtual size_t size() const { throw std::bad_cast(); }

		/**
		 * @brief Equality comparison with a non-Variant value.
		 *
		 * Converts this Variant to @c T via @c static_cast and compares.
		 *
		 * @warning Do not use @c T = const char*: the comparison becomes a pointer
		 *          comparison rather than a string comparison and will always return
		 *          @c false. Use @c std::string or string literals instead.
		 */
		template<class T>
		requires (!std::is_base_of_v<Variant, T>)
		bool operator==(const T& value) const {
			// Route 64-bit integers through the explicit stdint virtual to avoid
			// ambiguity on LP64 Linux where long long and int64_t (= long) are distinct
			// types and there is no operator long long() in the virtual interface.
			if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
				if constexpr (std::is_signed_v<T>)
					return value == static_cast<T>(static_cast<std::int64_t>(*this));
				else
					return value == static_cast<T>(static_cast<std::uint64_t>(*this));
			} else
				return value == static_cast<T>(*this);
		}

		/** @brief Deep equality: same DataType and same value. */
		bool operator==(const Variant& variant) const {
			return getType() == variant.getType() && equals(variant);
		}

	protected:
		virtual Variant* copy() const { throw std::bad_cast(); }
		virtual bool equals(const Variant&) const { throw std::bad_cast(); }

		// Convenience methods to read and write binary data
		// Wire format is little-endian. On LE hosts (x86, ARM, Apple Silicon) the
		// if-constexpr branch is eliminated at compile time — zero overhead.
		// byte-swap is its own inverse, so toLittleEndian serves both directions.
		template<class T>
		static T toLittleEndian(T value) {
			if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1)
				return value;
			else {
				char bytes[sizeof(T)];
				std::memcpy(bytes, &value, sizeof(T));
				for(size_t i = 0; i < sizeof(T) / 2; ++i)
					std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
				std::memcpy(&value, bytes, sizeof(T));
				return value;
			}
		}

		template<class T>
		static void writeBinary(std::ostream& ostream, T data) {
			T leData = toLittleEndian(data);
			ostream.write(reinterpret_cast<char*>(&leData), sizeof(T));
		}

		template<class T>
		static T readBinary(std::istream& istream) {
			T data;
			istream.read(reinterpret_cast<char*>(&data), sizeof(T));
			return toLittleEndian(data);
		}

		// Serialization; Subclasses will override as appropriate
		friend std::ostream& operator<< (std::ostream& ostream, Variant& data) {
			data.write(ostream);
			return ostream;
        }
		friend std::istream& operator>> (std::istream& istream, Variant& data) {
			data.read(istream);
			return istream;
        }

		virtual void write(std::ostream& ) {}
		virtual void read(std::istream& ) {}

		friend class Structure;
	};

	/**
	 * @brief Concrete Variant holding a single scalar value of type @c T.
	 *
	 * Supported types are the standard integer widths (@c uint8_t … @c int64_t,
	 * @c size_t), @c float, @c double, @c bool, and @c char. Using any other
	 * @c T will store a @c DataType::unsupported tag and emit a caution on write.
	 *
	 * @tparam T The stored scalar type.
	 */
	template<class T>
	class Primitive : public Variant {
	public:
		/** @brief Constructs a Primitive with the given value (default zero/false). */
		Primitive(T initializer={}) : value(initializer) {}
		DataType getType() const override { return getDataType(); }
		operator std::string() const override {
			// std::format("{}", value) gives identical results but is markedly slower for
			// plain numeric types; bool and char keep their non-numeric textual forms.
			if constexpr (std::is_same_v<T, bool>)
				return value ? "true" : "false";
			else if constexpr (std::is_same_v<T, char>)
				return std::string(1, value);
			else {
				char buffer[64];
				auto [end, errorCode] = std::to_chars(buffer, buffer + sizeof(buffer), value);
				return std::string(buffer, end);
			}
		};
		Variant *copy() const override {
			return new Primitive(value);
		}
		bool equals(const Variant& variant) const override {
			// Same LP64 long long / int64_t disambiguation as operator==.
			if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
				if constexpr (std::is_signed_v<T>)
					return static_cast<std::int64_t>(value) == static_cast<std::int64_t>(variant);
				else
					return static_cast<std::uint64_t>(value) == static_cast<std::uint64_t>(variant);
			} else
				return value == (T)variant;
		}

		operator char() const override { return value; }
		operator std::uint8_t() const override { return value; }
		operator std::uint16_t() const override { return value; }
		operator std::uint32_t() const override { return value; }
		operator std::uint64_t() const override { return value; }
#if defined(__APPLE__) || (defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ < 8)
		operator size_t() const override { return value; }
#endif
		operator std::int8_t() const override { return value; }
		operator std::int16_t() const override { return value; }
		operator std::int32_t() const override { return value; }
		operator std::int64_t() const override { return value; }
		operator float() const override { return value; }
		operator double() const override { return value; }
		operator bool() const override { return value; }

		size_t size() const override {
			return sizeof(T);
		}

		void write(std::ostream& ostream) override {
			if(getType() != DataType::unsupported) {
				T leValue = toLittleEndian(value);
				ostream.write(reinterpret_cast<const char*>(&leValue), sizeof(T));
			} else
				caution("Oms: Attempt to save an unsupported data type");
		}
		void read(std::istream& istream) override {
			istream.read(reinterpret_cast<char*>(&value), sizeof(T));
			value = toLittleEndian(value);
		}

	private:
		// Note: size_t and uint64_t may be the same type on some platforms (e.g. 64-bit
		// Linux/Windows), so these must stay as one if-constexpr chain rather than separate
		// explicit specializations, which would collide and fail to compile in that case.
		static constexpr DataType getDataType() {
			if constexpr (std::is_same_v<T, char> || std::is_same_v<T, std::int8_t>) return DataType::int8;
			else if constexpr (std::is_same_v<T, std::uint8_t>) return DataType::uint8;
			else if constexpr (std::is_same_v<T, std::uint16_t>) return DataType::uint16;
			else if constexpr (std::is_same_v<T, std::uint32_t>) return DataType::uint32;
			else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, size_t> || std::is_same_v<T, unsigned long long>) return DataType::uint64;
			else if constexpr (std::is_same_v<T, std::int16_t>) return DataType::int16;
			else if constexpr (std::is_same_v<T, std::int32_t>) return DataType::int32;
			else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, long long>) return DataType::int64;
			else if constexpr (std::is_same_v<T, float>) return DataType::float4;
			else if constexpr (std::is_same_v<T, double>) return DataType::float8;
			else if constexpr (std::is_same_v<T, bool>) return DataType::boolean;
			else return DataType::unsupported;
		}

		T value;
	};

	/**
	 * @brief Concrete Variant holding a UTF-8 string.
	 *
	 * Strings are stored on the wire as a @c size_t byte count (including a null
	 * terminator) followed by the character data. The null terminator is written
	 * to the wire but is stripped on read; the stored @c std::string value never
	 * contains an embedded null.
	 *
	 * @note There is no implicit @c const char* conversion; see @c Variant for the
	 *       rationale and the recommended @c std::string_literals workaround.
	 */
	class String : public Variant {
	public:
		/** @brief Constructs a String with the given initial value. */
		String(const std::string& initializer = {0}) : value(initializer) {}
		DataType getType() const override { return DataType::string; }
		Variant *copy() const override {
			return new String(value);
		}
		bool equals(const Variant& variant) const override {
			return value == (std::string)variant;
		}

		operator std::string() const override {
			return value;
		};

		/** @brief Number of characters in the string (excluding null terminator). */
		size_t size() const override {
			return value.size();
		}

		void write(std::ostream& ostream) override {
			size_t dataSize = value.size() + 1;
			writeBinary(ostream, dataSize);
			ostream.write(value.data(), dataSize);
		}
		void read(std::istream& istream) override {
			size_t dataSize = readBinary<size_t>(istream);
			throw_if<std::ios_base::failure>(dataSize == 0, "Oms: Invalid string length");
			value.resize(dataSize);
			istream.read(value.data(), dataSize);
			ensure(value[dataSize - 1] == 0);
			value.resize(dataSize - 1);
		}

	private:
		std::string value;
	};

	/**
	 * @brief Concrete Variant holding an opaque byte buffer.
	 *
	 * The raw bytes are copied in on construction and written verbatim to the wire.
	 * Access the buffer via @c operator void*() (returns a pointer valid for the
	 * lifetime of this object) and @c size() (byte count).
	 */
	class Blob : public Variant {
	public:
		/**
		 * @brief Constructs a Blob, copying @p blobSize bytes from @p blob.
		 *
		 * @param blob     Pointer to the source data (may be @c nullptr if @p blobSize is 0).
		 * @param blobSize Number of bytes to copy.
		 */
		Blob(const void* blob = nullptr, size_t blobSize = 0) : dataSize(blobSize) {
			data = std::make_unique<std::uint8_t[]>(dataSize);
			if(dataSize)
				memcpy(reinterpret_cast<char*>(data.get()), blob, dataSize);
		}
		DataType getType() const override { return DataType::blob; }
		Variant *copy() const override {
			return new Blob(data.get(), dataSize);
		}
		bool equals(const Variant& variant) const override {
			ensure(variant.getType() == getType());
			const Blob& blob = reinterpret_cast<const Blob&>(variant);
			return blob.dataSize == dataSize && memcmp(reinterpret_cast<char*>(data.get()), reinterpret_cast<char*>(blob.data.get()), dataSize) == 0;
		}

		/** @brief Number of bytes in the buffer. */
		size_t size() const override { return dataSize; }

		/** @brief Pointer to the internal data buffer (valid for the lifetime of this object). */
		operator void*() const override { return data.get(); }

		void write(std::ostream& ostream) override {
			writeBinary(ostream, dataSize);
			ostream.write(reinterpret_cast<char*>(data.get()), dataSize);
		}
		void read(std::istream& istream) override {
			dataSize = readBinary<size_t>(istream);
			data = std::make_unique<std::uint8_t[]>(dataSize);
			istream.read(reinterpret_cast<char*>(data.get()), dataSize);
		}

	private:
		size_t dataSize;
		std::unique_ptr<std::uint8_t[]> data;
	};

	/**
	 * @brief Concrete Variant holding a contiguous array of scalar elements of type @c T.
	 *
	 * Supported element types mirror those of @c Primitive<T>. Elements are stored and
	 * iterated in their original order. The wire format writes a @c size_t element count
	 * followed by the raw element bytes in little-endian order.
	 *
	 * @tparam T Element type (must be a type supported by @c Primitive<T>).
	 */
	template<class T>
	class Vector : public Variant {
	public:
		/** @brief Constructs a Vector from an @c std::vector, copying all elements. */
		Vector(const std::vector<T>& data) : Vector(data.data(), data.size()) {}

		/**
		 * @brief Constructs a Vector by copying @p count elements from @p data.
		 *
		 * @param data  Pointer to the source elements (may be @c nullptr if @p count is 0).
		 * @param count Number of elements to copy.
		 */
		Vector(const T* data = nullptr, size_t count = 0) : count(count) {
			this->data = std::make_unique<T[]>(count);
			if(count)
				memcpy(reinterpret_cast<char*>(this->data.get()), data, count * sizeof(T));
		}
		DataType getType() const override { return getDataType(); }

		/** @brief Returns the DataType tag for this Vector's element type (compile-time constant). */
		static constexpr DataType dataType() { return getDataType(); }

		Variant *copy() const override {
			return new Vector(data.get(), count);
		}
		bool equals(const Variant& variant) const override {
			ensure(variant.getType() == getType());
			const Vector<T>& vector = reinterpret_cast<const Vector<T>&>(variant);
			return vector.count == count && memcmp(reinterpret_cast<char*>(data.get()), reinterpret_cast<char*>(vector.data.get()), count * sizeof(T)) == 0;
		}

		/** @brief Number of elements. */
		size_t size() const override { return count; }

		/** @brief Pointer to the internal element buffer (valid for the lifetime of this object). */
		operator void*() const override { return data.get(); }

		/** @brief Indexed element access (no bounds checking). */
	    T& operator[](std::size_t index) {
			return data[index];
		}
		/** @brief Indexed element access (no bounds checking). */
	    const T& operator[](std::size_t index) const {
			return data[index];
		}

		/** @brief Iterator to the first element. */
		constexpr T* begin() {
			return data.get();
		}
		/** @brief Past-the-end iterator. */
		constexpr T* end() {
			return data.get() + count;
		}

		void write(std::ostream& ostream) override {
			writeBinary(ostream, count);
			// Fast path: bulk write on LE or single-byte elements (no swap needed).
			// Big-endian: write element by element through writeBinary to swap each one.
			if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1)
				ostream.write(reinterpret_cast<char*>(data.get()), count * sizeof(T));
			else
				for(size_t i = 0; i < count; ++i)
					writeBinary(ostream, data[i]);
		}
		void read(std::istream& istream) override {
			count = readBinary<size_t>(istream);
			data = std::make_unique<T[]>(count);
			if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1)
				istream.read(reinterpret_cast<char*>(data.get()), count * sizeof(T));
			else
				for(size_t i = 0; i < count; ++i)
					data[i] = readBinary<T>(istream);
		}

	private:
		// See the equivalent note in Primitive<T>::getDataType: size_t and uint64_t can be
		// the same type on some platforms, so this must stay one if-constexpr chain.
		static constexpr DataType getDataType() {
			if constexpr (std::is_same_v<T, char> || std::is_same_v<T, std::int8_t>) return DataType::int8v;
			else if constexpr (std::is_same_v<T, std::uint8_t>) return DataType::uint8v;
			else if constexpr (std::is_same_v<T, std::uint16_t>) return DataType::uint16v;
			else if constexpr (std::is_same_v<T, std::uint32_t>) return DataType::uint32v;
			else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, size_t> || std::is_same_v<T, unsigned long long>) return DataType::uint64v;
			else if constexpr (std::is_same_v<T, std::int16_t>) return DataType::int16v;
			else if constexpr (std::is_same_v<T, std::int32_t>) return DataType::int32v;
			else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, long long>) return DataType::int64v;
			else if constexpr (std::is_same_v<T, float>) return DataType::float4v;
			else if constexpr (std::is_same_v<T, double>) return DataType::float8v;
			else return DataType::unsupported;
		}

		size_t count;
		std::unique_ptr<T[]> data;
	};

	/**
	 * @brief Ordered, key-value container — the primary OMS building block.
	 *
	 * A Structure holds a sequence of named Variant members in insertion order.
	 * Member names are arbitrary UTF-8 strings up to 255 bytes. The wire format
	 * caps member count at 65535 (overridable by defining @c MemberCount before
	 * including this header).
	 *
	 * Structure is used directly as well as as the base class of Section. All
	 * @c add* methods overwrite an existing member with the same name; all
	 * @c getOrAdd* methods leave an existing member untouched.
	 */
	class Structure : public Variant {
	public:
		/**
		 * @brief Constructs an empty Structure.
		 *
		 * The @p index parameter is set internally by @c Array::addStructure() to
		 * record the element's position within its parent array. External callers
		 * should use the default.
		 */
		Structure(std::optional<std::size_t> index = {}) : index(index) {}

		/** @brief Deep-copies another Structure, including all nested members. */
		Structure(const Structure& structure) : index(structure.index) {
			members.reserve(structure.members.size());
			for(auto& [identifier, variant] : structure.members)
				members.emplace_back(identifier, std::unique_ptr<Variant>(variant->copy()));
		}
		DataType getType() const override { return DataType::structure; }
		Variant* copy() const override {
			Structure* structure = new Structure();
			structure->members.reserve(members.size());
			for(auto& [identifier, variant] : members)
				structure->members.emplace_back(identifier, std::unique_ptr<Variant>(variant->copy()));
			return structure;
		}
		bool equals(const Variant& variant) const override {
			ensure(variant.getType() == getType());
			const Structure& structure = reinterpret_cast<const Structure&>(variant);
			if(structure.members.size() != members.size()) return false;
			for(auto& [identifier, member] : members) {
				auto it = structure.findMember(identifier);
				if(it == structure.members.end() || !it->second->equals(*member)) return false;
			}
			return true;
		}

		/** @brief Returns @c true if the Structure has no members. */
		bool empty() const noexcept {
			return members.empty();
		}

		/** @brief Returns the number of members. */
		size_t size() const override {
			return members.size();
		}

		/**
		 * @brief Returns @c true if a member with the given name exists.
		 *
		 * @param identifier Member name to look up.
		 */
		bool contains(const std::string& identifier) const {
			return findMember(identifier) != members.end();
		}

		/**
		 * @brief Returns the member with the given name.
		 *
		 * @throws std::out_of_range if the member does not exist.
		 */
		const Variant& operator[](const char* identifier) const override {
			return at(identifier);
		}
		/** @copydoc operator[](const char*) */
		const Variant& operator[](const std::string& identifier) const override {
			return at(identifier);
		}

		/**
		 * @brief Returns the member with the given name.
		 *
		 * Equivalent to @c operator[]. Provided for readability in contexts where
		 * the subscript syntax would be ambiguous.
		 *
		 * @throws std::out_of_range if the member does not exist.
		 */
		const Variant& get(const std::string& identifier) const {
			return at(identifier);
		}

		/**
		 * @brief Copies a blob member into an @c std::vector<T>.
		 *
		 * The stored value must be a @c DataType::blob; @p vector is resized to
		 * @c blob.size() / @c sizeof(T) elements and the raw bytes are reinterpreted
		 * as @c T values.
		 *
		 * @tparam T     Element type for the output vector.
		 * @param identifier  Member name.
		 * @param vector      Output vector; receives the blob data.
		 * @throws std::bad_cast    if the member is not a Blob.
		 * @throws std::out_of_range if the member does not exist.
		 */
		template<class T>
		void get(const std::string& identifier, std::vector<T>& vector) {
			Variant& variant = at(identifier);
			throw_if<std::bad_cast>(variant.getType() != DataType::blob);
			Blob& blob = reinterpret_cast<Blob&>(variant);
			vector.resize(blob.size() / sizeof(T));
			memcpy(vector.data(), static_cast<void*>(blob), blob.size());
		}

		/**
		 * @brief Returns the member cast to @c T, or @p defaultValue if the member is absent.
		 *
		 * Never inserts a member; safe to call on a @c const Structure.
		 *
		 * @tparam T            Target type for the cast (must be supported by the stored Variant).
		 * @param identifier    Member name.
		 * @param defaultValue  Value to return when the member is absent.
		 * @return              The stored value cast to @c T, or @p defaultValue.
		 *
		 * @warning Do not use @c T = const char*: @c static_cast<const char*>(variant)
		 *          returns a pointer into the String's internal buffer that dangles
		 *          immediately. Use @c T = std::string instead.
		 */
		template<class T>
		T getOr(const std::string& identifier, T defaultValue) const {
			auto it = findMember(identifier);
			if(it == members.end()) return defaultValue;
			return static_cast<T>(*it->second);
		}

		/**
		 * @brief Returns a reference to the member, inserting a new one if absent.
		 *
		 * If a member named @p identifier already exists it is returned unchanged;
		 * the existing value is @em not overwritten. If absent, a new member is
		 * constructed from @p value and inserted at the end.
		 *
		 * @tparam T          Value type (scalar, @c std::string, or @c const char*).
		 * @param identifier  Member name.
		 * @param value       Initial value used only when inserting.
		 * @return            Reference to the (existing or newly created) member.
		 */
		template<class T>
		Variant& getOrAdd(const std::string& identifier, T value) {
			return findOrInsert(identifier, [&] { return makeValue(value); });
		}

		/**
		 * @brief Returns a reference to a blob member, inserting a new one if absent.
		 *
		 * If absent, a new @c Blob is created by copying @p blobSize bytes from
		 * @p blob and inserted at the end.
		 *
		 * @param identifier  Member name.
		 * @param blob        Source data used only when inserting.
		 * @param blobSize    Byte count used only when inserting.
		 * @return            Reference to the (existing or newly created) member.
		 */
		Variant& getOrAdd(const std::string& identifier, const void* blob, size_t blobSize) {
			return findOrInsert(identifier, [&] { return std::make_unique<Blob>(blob, blobSize); });
		}

		/**
		 * @brief Returns a reference to a nested Structure, inserting an empty one if absent.
		 *
		 * @param identifier  Member name.
		 * @return            Reference to the (existing or newly created) nested Structure.
		 * @throws std::bad_cast if the member exists but is not a Structure.
		 */
		Structure& getOrAddStructure(const std::string& identifier) {
			Variant& variant = findOrInsert(identifier, [] { return std::make_unique<Structure>(); });
			throw_if<std::bad_cast>(variant.getType() != DataType::structure);
			return reinterpret_cast<Structure&>(variant);
		}

		/**
		 * @brief Returns a reference to a nested Array, inserting an empty one if absent.
		 *
		 * @param identifier  Member name.
		 * @return            Reference to the (existing or newly created) nested Array.
		 * @throws std::bad_cast if the member exists but is not an Array.
		 */
		Array& getOrAddArray(const std::string& identifier) {
			Variant& variant = findOrInsert(identifier,
				[] { return std::unique_ptr<Variant>(create(DataType::array)); });
			throw_if<std::bad_cast>(variant.getType() != DataType::array);
			return reinterpret_cast<Array&>(variant);
		}

		/**
		 * @brief Returns a reference to a typed Vector member, inserting an empty one if absent.
		 *
		 * @tparam T          Element type of the Vector.
		 * @param identifier  Member name.
		 * @return            Reference to the (existing or newly created) Vector.
		 * @throws std::bad_cast if the member exists but has a different element type.
		 */
		template<class T>
		Vector<T>& getOrAddVector(const std::string& identifier) {
			Variant& variant = findOrInsert(identifier, [] { return std::make_unique<Vector<T>>(); });
			throw_if<std::bad_cast>(variant.getType() != Vector<T>::dataType());
			return reinterpret_cast<Vector<T>&>(variant);
		}

		/**
		 * @brief Returns the member names in insertion order.
		 *
		 * @return @c std::vector<std::string> containing each member's name, in insertion order.
		 */
		std::vector<std::string> getEntries() const {
			std::vector<std::string> entries;
			entries.reserve(members.size());
			for(auto& [identifier, variant] : members)
				entries.push_back(identifier);
			return entries;
		}

		/**
		 * @brief Adds or overwrites a scalar or string member.
		 *
		 * If a member named @p identifier already exists it is replaced.
		 *
		 * @tparam T         Value type (scalar, @c std::string, or @c const char*).
		 * @param identifier Member name.
		 * @param value      Value to store.
		 */
		template<class T>
		void add(const std::string& identifier, T value) {
			set(identifier, makeValue(value));
		}

		/**
		 * @brief Adds or overwrites a member by deep-copying an existing Variant.
		 *
		 * @param identifier Member name.
		 * @param value      Source Variant; a deep copy is made and stored.
		 */
		void addVariant(const std::string& identifier, const Variant& value) {
			set(identifier, std::unique_ptr<Variant>(value.copy()));
		}

		/**
		 * @brief Adds or overwrites a blob member, copying @p blobSize bytes from @p blob.
		 *
		 * @param identifier Member name.
		 * @param blob       Source data.
		 * @param blobSize   Byte count.
		 */
		void add(const std::string& identifier, const void* blob, size_t blobSize) {
			set(identifier, std::make_unique<Blob>(blob, blobSize));
		}

		/**
		 * @brief Adds or overwrites a blob member from an @c std::vector<T>.
		 *
		 * The raw byte representation of the vector is stored as a @c Blob.
		 *
		 * @tparam T         Element type.
		 * @param identifier Member name.
		 * @param blob       Source vector.
		 */
		template<class T>
		void add(const std::string& identifier, const std::vector<T>& blob) {
			add(identifier, blob.data(), sizeof(T) * blob.size());
		}

		/**
		 * @brief Adds or replaces a nested Structure member and returns a reference to it.
		 *
		 * If a member with the same name exists it is replaced with a new empty Structure.
		 *
		 * @param identifier Member name.
		 * @return           Reference to the newly created nested Structure.
		 */
		Structure& addStructure(const std::string& identifier) {
			auto* structure = new Structure();
			set(identifier, std::unique_ptr<Structure>(structure));
			return *structure;
		}

		/**
		 * @brief Adds or replaces a nested Array member and returns a reference to it.
		 *
		 * If a member with the same name exists it is replaced with a new empty Array.
		 *
		 * @param identifier Member name.
		 * @return           Reference to the newly created nested Array.
		 */
		Array& addArray(const std::string& identifier) {
			Variant* array = create(DataType::array);
			set(identifier, std::unique_ptr<Variant>(array));
			return *reinterpret_cast<Array*>(array);
		}

		/**
		 * @brief Adds or replaces a Vector member from an @c std::vector and returns a reference.
		 *
		 * @tparam T         Element type.
		 * @param identifier Member name.
		 * @param stdVector  Source data; all elements are copied.
		 * @return           Reference to the newly created Vector.
		 */
		template<class T>
		Vector<T>& addVector(const std::string& identifier, const std::vector<T>& stdVector) {
			auto* omsVector = new Vector<T>(stdVector);
			set(identifier, std::unique_ptr<Vector<T>>(omsVector));
			return *omsVector;
		}

		/**
		 * @brief Adds or replaces a Vector member from a raw array and returns a reference.
		 *
		 * @tparam T         Element type.
		 * @param identifier Member name.
		 * @param data       Pointer to source elements (may be @c nullptr if @p count is 0).
		 * @param count      Number of elements to copy.
		 * @return           Reference to the newly created Vector.
		 */
		template<class T>
		Vector<T>& addVector(const std::string& identifier, const T* data = nullptr, size_t count = 0) {
			auto* omsVector = new Vector<T>(data, count);
			set(identifier, std::unique_ptr<Vector<T>>(omsVector));
			return *omsVector;
		}

		/**
		 * @brief Adds or replaces a Vector member from an initializer list and returns a reference.
		 *
		 * @tparam T         Element type.
		 * @param identifier Member name.
		 * @param initializer Brace-enclosed list of initial elements.
		 * @return           Reference to the newly created Vector.
		 */
		template<class T>
		Vector<T>& addVector(const std::string& identifier, std::initializer_list<T> initializer) {
			auto* omsVector = new Vector<T>(initializer.begin(), initializer.size());
			set(identifier, std::unique_ptr<Vector<T>>(omsVector));
			return *omsVector;
		}

		/** @brief Removes all members. */
		void clear() noexcept {
			members.clear();
		}

		/**
		 * @brief The zero-based index of this Structure within its parent Array, if any.
		 *
		 * Set automatically by @c Array::addStructure(); @c std::nullopt for Structures
		 * that are not Array elements. Read-only.
		 */
		const std::optional<std::size_t> index;

	protected:
		void write(std::ostream& ostream) override {
			throw_if<std::overflow_error>(members.size() > std::numeric_limits<MemberCount>::max(),
				std::format("Member count limitted to {}.  Try defining MemberCount type.", std::numeric_limits<MemberCount>::max()));
			writeBinary<MemberCount>(ostream, members.size());
			for(auto& [identifier, item] : members) {
				writeIdentifier(ostream, identifier);
				Variant::writeBinary(ostream, item->getType());
				item->write(ostream);
			}
		}

		void read(std::istream& istream) override {
			MemberCount memberCount = readBinary<MemberCount>(istream);
			members.reserve(memberCount);
			for(unsigned int memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
				std::string identifier = readIdentifier(istream);
				DataType type = static_cast<DataType>(Variant::readBinary<std::uint8_t>(istream));
				Variant* instance = create(static_cast<DataType>(type));
				instance->read(istream);
				members.emplace_back(std::move(identifier), std::unique_ptr<Variant>(instance));
			}
		}

		static void writeIdentifier(std::ostream& ostream, const std::string& identifier) {
			throw_if<std::overflow_error>(identifier.size() > std::numeric_limits<std::uint8_t>::max(),
				std::format("Identifier length limitted to {}.", std::numeric_limits<std::uint8_t>::max()));
			writeBinary<std::uint8_t>(ostream, identifier.size());
			ostream.write(identifier.c_str(), identifier.size() + 1);
		}

		static std::string readIdentifier(std::istream& istream) {
			std::uint8_t identifierSize = readBinary<std::uint8_t>(istream);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
			char buffer[identifierSize + 1];
#pragma clang diagnostic pop

			istream.read(buffer, identifierSize + 1);
			return std::string(buffer);
		}

	private:
		friend class Array;

		// members is a flat vector rather than a hash map: the wire format caps identifiers
		// at 255 bytes and member count at 65535, so structures are small by design.
		// At that scale a linear scan over contiguous memory beats hashing + per-node heap
		// allocation, and insertion order is preserved as a free bonus.
		using Members = std::vector<std::pair<std::string, std::unique_ptr<Variant>>>;

		template<class T>
		static std::unique_ptr<Variant> makeValue(T value) {
			if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>)
				return std::make_unique<String>(value);
			else
				return std::make_unique<Primitive<T>>(value);
		}

		Members::iterator findMember(const std::string& identifier) {
			return std::find_if(members.begin(), members.end(),
				[&](const auto& entry) { return entry.first == identifier; });
		}
		Members::const_iterator findMember(const std::string& identifier) const {
			return std::find_if(members.begin(), members.end(),
				[&](const auto& entry) { return entry.first == identifier; });
		}

		Variant& at(const std::string& identifier) {
			auto it = findMember(identifier);
			throw_if<std::out_of_range>(it == members.end(), identifier);
			return *it->second;
		}
		const Variant& at(const std::string& identifier) const {
			auto it = findMember(identifier);
			throw_if<std::out_of_range>(it == members.end(), identifier);
			return *it->second;
		}

		// Inserts (via construct()) when identifier is absent; returns the existing value otherwise.
		template<class ConstructFn>
		Variant& findOrInsert(const std::string& identifier, ConstructFn construct) {
			auto it = findMember(identifier);
			if(it != members.end())
				return *it->second;
			members.emplace_back(identifier, construct());
			return *members.back().second;
		}

		// Inserts when absent, overwrites when present.
		void set(const std::string& identifier, std::unique_ptr<Variant> value) {
			auto it = findMember(identifier);
			if(it != members.end())
				it->second = std::move(value);
			else
				members.emplace_back(identifier, std::move(value));
		}

		Members members;
	};

	/**
	 * @brief Ordered sequence of Structures, all sharing the same implicit schema.
	 *
	 * An Array stores zero or more Structure elements in insertion order. Each element
	 * records its own position via @c Structure::index. Arrays are created through
	 * @c Structure::addArray() or @c Structure::getOrAddArray(); elements are appended
	 * with @c addStructure().
	 *
	 * The wire format writes a @c uint32_t element count (overridable by defining
	 * @c ItemCount before including this header) followed by each Structure body in order.
	 */
	class Array : public Variant {
	public:
		DataType getType() const override { return DataType::array; }
		Variant *copy() const override {
			Array* array = new Array();
			for(const std::unique_ptr<Structure>& structure : members)
				array->members.push_back(std::unique_ptr<Structure>(static_cast<Structure*>(structure->copy())));
			return array;
		}
		bool equals(const Variant& variant) const override {
			ensure(variant.getType() == getType());
			const Array& array = reinterpret_cast<const Array&>(variant);
			bool isSame = array.members.size() == members.size();
			for(unsigned int index = 0; index < members.size() && isSame; ++index)
				isSame &= array.members[index]->equals(*members[index]);
			return isSame;
		}

		/** @brief Returns @c true if the Array has no elements. */
		constexpr bool empty() const noexcept{
			return members.empty();
		}

		/** @brief Returns the number of elements. */
		constexpr size_t size() const override {
			return members.size();
		}

		/** @brief Indexed element access (no bounds checking). */
	    Structure& operator[](std::size_t index) {
			return *members[index];
		}
		/** @brief Indexed element access (no bounds checking). */
	    const Structure& operator[](std::size_t index) const {
			return *members[index];
		}

		/**
		 * @brief Appends a new empty Structure and returns a reference to it.
		 *
		 * The new element's @c Structure::index is set to its position in the array.
		 *
		 * @return Reference to the newly appended Structure.
		 */
		Structure& addStructure() {
			members.push_back(std::make_unique<Structure>(members.size()));
			return *members.back();
		}

	protected:
		void write(std::ostream& ostream) override {
			throw_if<std::overflow_error>(members.size() > std::numeric_limits<ItemCount>::max(),
				std::format("Item count limitted to {}.  Try defining custom ItemCount type.", std::numeric_limits<ItemCount>::max()));
			writeBinary<ItemCount>(ostream, static_cast<ItemCount>(members.size()));
			for(auto& member : members)
				member->write(ostream);
		}
		void read(std::istream& istream) override {
			ItemCount arraySize = readBinary<ItemCount>(istream);
			members.reserve(arraySize);
			for(size_t index = 0; index < arraySize; ++index) {
				members.push_back(std::make_unique<oms::Structure>(index));
				members.back()->read(istream);
			}
		}

	private:
		std::vector<std::unique_ptr<Structure>> members;
	};

	constexpr static inline Variant* create(DataType type) {
		Variant* instance = nullptr;
		switch(type) {
		case DataType::structure:
			instance = new Structure();
			break;
		case DataType::array:
			instance = new Array();
			break;
		case DataType::string:
			instance = new String();
			break;
		case DataType::uint8:
			instance = new Primitive<std::uint8_t>();
			break;
		case DataType::uint16:
			instance = new Primitive<std::uint16_t>();
			break;
		case DataType::uint32:
			instance = new Primitive<std::uint32_t>();
			break;
		case DataType::uint64:
			instance = new Primitive<std::uint64_t>();
			break;
		case DataType::int8:
			instance = new Primitive<std::int8_t>();
			break;
		case DataType::int16:
			instance = new Primitive<std::int16_t>();
			break;
		case DataType::int32:
			instance = new Primitive<std::int32_t>();
			break;
		case DataType::int64:
			instance = new Primitive<std::int64_t>();
			break;
		case DataType::float4:
			instance = new Primitive<float>();
			break;
		case DataType::float8:
			instance = new Primitive<double>();
			break;
		case DataType::boolean:
			instance = new Primitive<bool>();
			break;
		case DataType::blob:
			instance = new Blob();
			break;
		case DataType::uint8v:
			instance = new Vector<std::uint8_t>();
			break;
		case DataType::uint16v:
			instance = new Vector<std::uint16_t>();
			break;
		case DataType::uint32v:
			instance = new Vector<std::uint32_t>();
			break;
		case DataType::uint64v:
			instance = new Vector<std::uint64_t>();
			break;
		case DataType::int8v:
			instance = new Vector<std::int8_t>();
			break;
		case DataType::int16v:
			instance = new Vector<std::int16_t>();
			break;
		case DataType::int32v:
			instance = new Vector<std::int32_t>();
			break;
		case DataType::int64v:
			instance = new Vector<std::int64_t>();
			break;
		case DataType::float4v:
			instance = new Vector<float>();
			break;
		case DataType::float8v:
			instance = new Vector<double>();
			break;
		case DataType::unsupported:
		case DataType::_last:
			throw std::bad_typeid();
			break;
		}
		return instance;
	}

	/**
	 * @brief GUID-framed container that wraps a Structure for streaming and random access.
	 *
	 * Section adds a binary header (two 64-bit GUIDs, three reserved @c size_t fields,
	 * and a name identifier) before the Structure body. The total byte count is filled
	 * in on seekable streams, enabling callers to skip sections without deserializing
	 * their bodies via @c findNext().
	 *
	 * **Streaming write pattern** — a single Section object can be reused across many
	 * writes; @c operator<< clears it automatically after each flush:
	 * @code
	 * oms::Section section;
	 * for(auto& record : records) {
	 *     section.name = "record";
	 *     section.add("id", record.id);
	 *     out << section;   // writes and clears
	 * }
	 * @endcode
	 *
	 * **Random-access read pattern** — locate a section by name without reading
	 * the entire file:
	 * @code
	 * std::ifstream in("data.oms", std::ios::binary);
	 * auto result = oms::Section::findNext(in, "config");
	 * if(result) { ... }
	 * @endcode
	 */
	class Section : public Structure {
	public:
		/** @brief The section's name, written to and read from the wire. */
		std::string name;

		/** @brief Returns a human-readable description of the form @c "Section (name)". */
		operator std::string() const override {
			return std::format("Section ({})", name);
		};

		/** @brief Clears all members and resets the name to an empty string. */
		void clear() noexcept {
			Structure::clear();
			name.clear();
		}

		/**
		 * @brief Total byte count of this section as recorded in the header.
		 *
		 * Covers the range from the first GUID byte through the last byte of the body.
		 * A value of zero means the section was written to a non-seekable stream (e.g. a
		 * pipe) and the size field was not filled in. When non-zero, callers can skip
		 * forward without deserializing the body:
		 * @code
		 * auto start = in.tellg();
		 * in >> section;
		 * // To revisit: in.seekg(start + section.sectionSize());
		 * @endcode
		 */
		size_t sectionSize() const { return address1; }

		/**
		 * @brief Scans forward from the current stream position for a named section.
		 *
		 * Non-matching sections are skipped efficiently using the stored size field when
		 * available (seekable streams written by a current writer); on non-seekable streams
		 * or files written without size information, each skipped section is fully
		 * deserialized to advance the stream position.
		 *
		 * To search from the beginning of the file, call @c istream.seekg(0) first.
		 * The method will not wrap around: if the named section appears before the
		 * current stream position it will not be found.
		 *
		 * @param istream    Input stream to scan.
		 * @param targetName Section name to find.
		 * @return           The matching Section, or @c std::nullopt if exhausted.
		 * @throws std::ios_base::failure if a section header's GUID is invalid.
		 */
		static std::optional<Section> findNext(std::istream& istream, const std::string& targetName) {
			while(istream.good()) {
				auto sectionStart = istream.tellg();

				const std::uint64_t guid1Check = readBinary<std::uint64_t>(istream);
				if(istream.eof()) return std::nullopt;
				const std::uint64_t guid2Check = readBinary<std::uint64_t>(istream);
				throw_if<std::ios_base::failure>(guid1Check != GUID1 || guid2Check != GUID2,
					"Oms: invalid section header");

				const size_t totalSize = readBinary<size_t>(istream);  // address1
				readBinary<size_t>(istream);                            // address2 (reserved)
				readBinary<size_t>(istream);                            // address3 (reserved)
				std::string sectionName = readIdentifier(istream);

				if(sectionName == targetName) {
					Section result;
					result.name = std::move(sectionName);
					result.address1 = totalSize;
					result.Structure::read(istream);
					return result;
				}

				// Not the right section. Skip forward without reading the body if the
				// section size is recorded and the stream is seekable; otherwise read
				// and discard the body to advance the stream position.
				if(totalSize != 0 && sectionStart != std::streampos(-1))
					istream.seekg(static_cast<std::streamoff>(sectionStart) +
					              static_cast<std::streamoff>(totalSize));
				else {
					Section discard;
					discard.Structure::read(istream);
				}
			}
			return std::nullopt;
		}

	protected:
		void write(std::ostream& ostream) override {
			auto sectionStart = ostream.tellp();
			writeBinary<std::uint64_t>(ostream, GUID1);
			writeBinary<std::uint64_t>(ostream, GUID2);
			auto sizeFieldPos = ostream.tellp();
			writeBinary(ostream, static_cast<size_t>(0));  // address1: total byte count (filled below)
			writeBinary(ostream, static_cast<size_t>(0));  // address2: reserved
			writeBinary(ostream, static_cast<size_t>(0));  // address3: reserved
			writeIdentifier(ostream, name);
			Structure::write(ostream);

			// Seek back and fill address1 with the total section byte count so readers
			// can forward-skip without deserializing the body. Degrades gracefully on
			// non-seekable streams (pipes, etc.) where tellp() returns -1.
			if(sectionStart != std::streampos(-1)) {
				auto endPos = ostream.tellp();
				if(endPos != std::streampos(-1)) {
					ostream.seekp(sizeFieldPos);
					writeBinary(ostream, static_cast<size_t>(endPos - sectionStart));
					ostream.seekp(endPos);
				}
			}

			clear();
		}

		void read(std::istream& istream) override {
			clear();

			const std::uint64_t guid1Check = readBinary<std::uint64_t>(istream);
			if(!istream.eof()) {
				const std::uint64_t guid2Check = readBinary<std::uint64_t>(istream);
				throw_if<std::ios_base::failure>(guid1Check != GUID1, "Invalid section");
				throw_if<std::ios_base::failure>(guid2Check != GUID2, "Invalid section");
				address1 = readBinary<size_t>(istream);
				address2 = readBinary<size_t>(istream);
				address3 = readBinary<size_t>(istream);
				name = readIdentifier(istream);
				Structure::read(istream);
			}
		}

		size_t address1 = 0;
		size_t address2 = 0;
		size_t address3 = 0;
	};
};
