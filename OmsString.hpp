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
 * @file OmsString.hpp
 * @brief Human-readable text formatting for OMS data structures.
 *
 * Include this header alongside @c Oms.hpp when you need to inspect or log OMS
 * data. The primary entry point is @c oms::toString(), which converts any Structure
 * (including a Section) to an indented, JSON-like string.
 *
 * @code
 * oms::Section section;
 * // ... populate section ...
 * std::cout << oms::toString(section) << '\n';
 * @endcode
 *
 * The helper functions (@c dumpStructure, @c dumpArray, @c dumpSimple, @c dumpVector)
 * are also available for callers that need to embed OMS output into a larger stream.
 */

#pragma once

#include <sstream>
#include "Oms.hpp"

namespace oms {
	void dumpArray(std::stringstream& stream, const oms::Array& array, int indent);

	inline void indent(std::stringstream& stream, int indentation) {
		for(int indentationIndex = 0; indentationIndex < indentation; ++indentationIndex)
			stream << " ";
	}

	/**
	 * @brief Formats a scalar Variant value into @p stream.
	 *
	 * Handles all non-container, non-vector DataTypes: strings are quoted;
	 * booleans emit @c true / @c false; blobs emit the placeholder @c (blob).
	 * Returns @c false for types not handled by this function (containers and
	 * vectors), which the caller can use to fall through to @c dumpVector.
	 *
	 * @param stream  Destination stream.
	 * @param variant Variant to format.
	 * @return @c true if the type was handled, @c false otherwise.
	 */
	inline bool dumpSimple(std::stringstream& stream, const oms::Variant& variant) {
		bool success = true;
		switch(variant.getType()) {
		case oms::DataType::string:
			stream << '\"' << static_cast<std::string>(variant) << '\"';
			break;
		case oms::DataType::uint8:
			stream << static_cast<std::uint8_t>(variant);
			break;
		case oms::DataType::uint16:
			stream << static_cast<std::uint16_t>(variant);
			break;
		case oms::DataType::uint32:
			stream << static_cast<std::uint32_t>(variant);
			break;
		case oms::DataType::uint64:
			stream << static_cast<std::uint64_t>(variant);
			break;
		case oms::DataType::int8:
			stream << static_cast<std::int8_t>(variant);
			break;
		case oms::DataType::int16:
			stream << static_cast<std::int16_t>(variant);
			break;
		case oms::DataType::int32:
			stream << static_cast<std::int32_t>(variant);
			break;
		case oms::DataType::int64:
			stream << static_cast<std::int64_t>(variant);
			break;
		case oms::DataType::float4:
			stream << static_cast<float>(variant);
			break;
		case oms::DataType::float8:
			stream << static_cast<double>(variant);
			break;
		case oms::DataType::boolean:
			stream << static_cast<std::string>(variant);  // "true" or "false"
			break;
		case oms::DataType::blob:
			stream << "(blob)";
			break;
		default:
			success = false;
			break;
		}
		return success;
	}

	/**
	 * @brief Formats a typed Vector into @p stream as a comma-separated bracketed list.
	 *
	 * Example output: @c [1, 2, 3]
	 *
	 * @tparam T      Element type of the Vector.
	 * @param stream  Destination stream.
	 * @param variant Vector to format.
	 */
	template<class T>
	inline void dumpVector(std::stringstream& stream, const oms::Vector<T>& variant) {
		stream << "[";
		for(size_t index = 0; index < variant.size(); ++index) {
			if(index > 0)
				stream << ", ";
			stream << variant[index];
		}
		stream << "]";
	}

	/**
	 * @brief Dispatches to the typed @c dumpVector overload based on the Variant's DataType.
	 *
	 * Returns @c false for non-vector types, which the caller can use as a signal that
	 * the value could not be formatted.
	 *
	 * @param stream  Destination stream.
	 * @param variant Variant to format (should be a Vector type).
	 * @return @c true if the type was handled, @c false otherwise.
	 */
	inline bool dumpVector(std::stringstream& stream, const oms::Variant& variant) {
		bool success = true;
		switch(variant.getType()) {
		case oms::DataType::uint8v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::uint8_t>&>(variant));
			break;
		case oms::DataType::uint16v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::uint16_t>&>(variant));
			break;
		case oms::DataType::uint32v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::uint32_t>&>(variant));
			break;
		case oms::DataType::uint64v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::uint64_t>&>(variant));
			break;
		case oms::DataType::int8v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::int8_t>&>(variant));
			break;
		case oms::DataType::int16v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::int16_t>&>(variant));
			break;
		case oms::DataType::int32v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::int32_t>&>(variant));
			break;
		case oms::DataType::int64v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<std::int64_t>&>(variant));
			break;
		case oms::DataType::float4v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<float>&>(variant));
			break;
		case oms::DataType::float8v:
			dumpVector(stream, reinterpret_cast<const oms::Vector<double>&>(variant));
			break;
		default:
			success = false;
			break;
		}
		return success;
	}

	/**
	 * @brief Formats a Structure into @p stream with indented, JSON-like syntax.
	 *
	 * Keys are emitted in alphabetical order. Nested Structures and Arrays are
	 * formatted recursively. If the Structure has a non-empty @c index (i.e. it is
	 * an Array element), the index is shown as a comment on the opening brace.
	 *
	 * @param stream        Destination stream.
	 * @param structure     Structure to format.
	 * @param indentation   Current indentation level in spaces.
	 */
	inline void dumpStructure(std::stringstream& stream, const oms::Structure& structure, int indentation) {
		if(structure.index)
			stream << std::format("{{ // {}", structure.index.value()) << std::endl;
		else
			stream << "{" << std::endl;

		indentation += 3;
		std::vector<std::string> entries = structure.getEntries();
		std::sort(entries.begin(), entries.end());
		for(size_t index = 0; index < entries.size(); ++index) {
			const std::string& entry = entries[index];
			indent(stream, indentation);
			stream << entry << ": ";
			const oms::Variant& variant = structure[entry];
			if(variant.getType() == oms::DataType::structure) {
				dumpStructure(stream, reinterpret_cast<const oms::Structure&>(variant), indentation);
			} else if(variant.getType() == oms::DataType::array) {
				dumpArray(stream, reinterpret_cast<const oms::Array&>(variant), indentation);
			} else {
				bool success = dumpSimple(stream, variant);
				if(!success)
					success = dumpVector(stream, variant);
				if(!success)
					stream << std::format("// Failed to dump field {}", entry) << std::endl;
			}

			if(index < entries.size() - 1)
				stream << ",";
			stream << std::endl;
		}
		indent(stream, indentation - 3);
		stream << "}";
	}

	/**
	 * @brief Formats an Array into @p stream as an indented bracketed sequence of Structures.
	 *
	 * Each element is formatted by @c dumpStructure. Elements are separated by commas.
	 *
	 * @param stream       Destination stream.
	 * @param array        Array to format.
	 * @param indentation  Current indentation level in spaces.
	 */
	inline void dumpArray(std::stringstream& stream, const oms::Array& array, int indentation) {
		stream << "[" << std::endl;
		indentation += 3;
		for(size_t index = 0; index < array.size(); ++index) {
			indent(stream, indentation);
			const oms::Structure& variant = array[index];
			dumpStructure(stream, reinterpret_cast<const oms::Structure&>(variant), indentation);
			if(index < array.size() - 1)
				stream << ",";
			stream << std::endl;
		}
		indent(stream, indentation - 3);
		stream << "]";
	}

	/**
	 * @brief Converts a Structure (or Section) to a human-readable string.
	 *
	 * The output is an indented, JSON-like representation of the Structure's
	 * members, with keys in alphabetical order. Nested Structures and Arrays are
	 * expanded recursively; vectors are formatted as comma-separated bracketed
	 * lists; blobs are shown as the placeholder @c (blob).
	 *
	 * @param structure  The Structure or Section to format.
	 * @return           A formatted multi-line string.
	 */
	inline std::string toString(const oms::Structure& structure) {
		std::stringstream stream;
		dumpStructure(stream, structure, 0);
		return stream.str();
	}
}