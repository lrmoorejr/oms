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
 * @file OmsText.hpp
 * @brief Lossless text form of OMS data, and the parser that reads it back.
 *
 * Include this header alongside @c Oms.hpp to write a Structure or a Section as text
 * and to turn that text back into the same data. Where @c oms::toString() (OmsString.hpp)
 * is for reading by eye, this text keeps everything: reading it back gives the same
 * members in the same order, each with the same DataType and the same value, bit for bit.
 * It is also meant to be written by hand.
 *
 * @verbatim
 * section "model" {
 *    splitPoint: 240,
 *    components: [
 *       {
 *          class: "Resynthesizer",
 *          visible: true,
 *          redAmplification: float4(39),
 *          "real time": 1723312000.25,
 *          point: int16[180, 94],
 *          signal: blob(0000803f 0000003f)
 *       }
 *    ]
 * }
 * @endverbatim
 *
 * | OMS type       | Text                                        | Notes |
 * |----------------|---------------------------------------------|-------|
 * | @c structure   | <tt>{ key: value, ... }</tt>                | Members keep their order. |
 * | @c array       | <tt>[ { ... }, { ... } ]</tt>               | A bare @c [ always holds structures. |
 * | @c string      | <tt>"text"</tt>                             | Escapes: <tt>\\" \\\\ \\n \\r \\t \\xHH</tt>. |
 * | @c boolean     | @c true, @c false                           | |
 * | @c int32       | @c 42, @c -7                                | What a bare integer means. |
 * | @c float8      | @c 0.5, @c 2e-3, @c nan, @c inf, @c -inf    | What a bare number with a @c . or an exponent means. |
 * | other scalars  | <tt>uint8(200)</tt>, <tt>float4(0.5)</tt>   | The type name, then the value. |
 * | vectors        | <tt>int16[180, 94]</tt>, <tt>float4[]</tt>  | The element type name, then the elements. |
 * | @c blob        | <tt>blob(0000803f 0000003f)</tt>            | Hexadecimal, two digits per byte. |
 *
 * The type names are @c uint8 to @c uint64, @c int8 to @c int64, @c float4 and @c float8.
 * A key is written bare when it is made of letters, digits, @c _ and @c . and does not
 * start with a digit or a @c . ; any other key is written as a quoted string. Members and
 * elements are separated by commas, and a trailing comma is allowed. Line breaks and
 * indentation carry no meaning. @c // starts a comment that runs to the end of the line.
 *
 * @code
 * oms::Structure settings = oms::fromText("{ gain: 0.5, taps: 3 }");
 * float gain = settings.getOr("gain", 1.0f);
 * std::cout << oms::toText(settings) << '\n';
 * @endcode
 */

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string_view>
#include "Oms.hpp"

namespace oms {

	/**
	 * @brief Controls how much of the data @c toText() and @c writeText() write out.
	 *
	 * By default everything is written. Lowering a limit gives a summary: a vector, blob or
	 * array over the limit is replaced by its size and a short hash of its contents, as in
	 * <tt>float4[4096 elided 9f3c2a1b]</tt>, <tt>blob(131072 bytes elided 9f3c2a1b)</tt> or
	 * <tt>[2008331 elided 9f3c2a1b]</tt>. The hash changes whenever the contents do; for an
	 * array it covers everything the array holds, whatever the other limits are. A summary
	 * cannot be read back: the parser rejects an elided value.
	 */
	struct TextOptions {
		size_t maxVectorElements = std::numeric_limits<size_t>::max();  ///< A vector with more elements than this is elided.
		size_t maxBlobBytes = std::numeric_limits<size_t>::max();       ///< A blob with more bytes than this is elided.
		size_t maxArrayElements = std::numeric_limits<size_t>::max();   ///< An array with more structures than this is elided.
	};

	/** @brief A place in a text. Lines and columns count from 1, and a column is a byte. */
	struct TextPosition {
		size_t line = 1;
		size_t column = 1;
	};

	/**
	 * @brief Thrown when text cannot be read as OMS data.
	 *
	 * @c what() gives the position and the reason in the form <tt>line:column: message</tt>,
	 * so prefixing it with a file name and a colon gives the usual compiler-style diagnostic.
	 */
	class ParseError : public std::runtime_error {
	public:
		ParseError(size_t line, size_t column, std::string_view message)
			: std::runtime_error(std::format("{}:{}: {}", line, column, message)), line(line), column(column) {}

		size_t line;    ///< Line of the error, counted from 1.
		size_t column;  ///< Column of the error in bytes, counted from 1.
	};

	/// @cond INTERNAL
	// Not called "detail": the ensure() macro names its own detail namespace without qualifying it
	namespace internal {

		struct NumericType {
			std::string_view name;
			DataType scalar;
			DataType vector;
		};

		// The numeric types under the names they have in text, which are their DataType names
		inline constexpr NumericType numericTypes[] = {
			{"uint8",  DataType::uint8,  DataType::uint8v},
			{"uint16", DataType::uint16, DataType::uint16v},
			{"uint32", DataType::uint32, DataType::uint32v},
			{"uint64", DataType::uint64, DataType::uint64v},
			{"int8",   DataType::int8,   DataType::int8v},
			{"int16",  DataType::int16,  DataType::int16v},
			{"int32",  DataType::int32,  DataType::int32v},
			{"int64",  DataType::int64,  DataType::int64v},
			{"float4", DataType::float4, DataType::float4v},
			{"float8", DataType::float8, DataType::float8v}
		};

		inline const NumericType* findNumericType(std::string_view name) {
			for(const NumericType& type : numericTypes)
				if(type.name == name)
					return &type;
			return nullptr;
		}

		// Finds a numeric type from either its scalar or its vector DataType
		inline const NumericType* findNumericType(DataType dataType) {
			for(const NumericType& type : numericTypes)
				if(type.scalar == dataType || type.vector == dataType)
					return &type;
			return nullptr;
		}

		// Calls visitor(std::type_identity<T>{}), where T is the C++ type behind a numeric scalar DataType
		template<class Visitor>
		void visitNumericType(DataType scalar, Visitor&& visitor) {
			switch(scalar) {
			case DataType::uint8:
				visitor(std::type_identity<std::uint8_t>{});
				break;
			case DataType::uint16:
				visitor(std::type_identity<std::uint16_t>{});
				break;
			case DataType::uint32:
				visitor(std::type_identity<std::uint32_t>{});
				break;
			case DataType::uint64:
				visitor(std::type_identity<std::uint64_t>{});
				break;
			case DataType::int8:
				visitor(std::type_identity<std::int8_t>{});
				break;
			case DataType::int16:
				visitor(std::type_identity<std::int16_t>{});
				break;
			case DataType::int32:
				visitor(std::type_identity<std::int32_t>{});
				break;
			case DataType::int64:
				visitor(std::type_identity<std::int64_t>{});
				break;
			case DataType::float4:
				visitor(std::type_identity<float>{});
				break;
			case DataType::float8:
				visitor(std::type_identity<double>{});
				break;
			default:
				ensure(false, "Oms: not a numeric scalar type");
				break;
			}
		}

		// Character classes are spelled out rather than taken from <cctype>, which follows the locale
		constexpr bool isDigit(int character) {
			return character >= '0' && character <= '9';
		}
		constexpr bool isLetter(int character) {
			return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
		}
		// A word is a bare key, a type name or a keyword: [A-Za-z_][A-Za-z0-9_.]*
		constexpr bool isWordStart(int character) {
			return isLetter(character) || character == '_';
		}
		constexpr bool isWordCharacter(int character) {
			return isWordStart(character) || isDigit(character) || character == '.';
		}
		// Letters are included so that "-inf" is one token, and so is a malformed number such as "12abc"
		constexpr bool isNumberCharacter(int character) {
			return isDigit(character) || isLetter(character) || character == '.' || character == '+' || character == '-';
		}
		// The value of a hexadecimal digit, or -1 if the character is not one
		constexpr int hexValue(int character) {
			if(isDigit(character))
				return character - '0';
			if(character >= 'a' && character <= 'f')
				return character - 'a' + 10;
			if(character >= 'A' && character <= 'F')
				return character - 'A' + 10;
			return -1;
		}

		constexpr size_t maxIdentifierSize = std::numeric_limits<std::uint8_t>::max();

		// Section keeps its two reserved header fields protected, so they are read through a derived class
		struct SectionHeader : Section {
			static bool usesReservedFields(const Section& section) {
				return section.*(&SectionHeader::address2) != 0 || section.*(&SectionHeader::address3) != 0;
			}
		};

		// 32-bit FNV-1a, which can be carried on from one run of bytes to the next
		constexpr std::uint32_t hashStart = 2166136261u;

		inline std::uint32_t hashBytes(std::uint32_t hash, const unsigned char* bytes, size_t count) {
			for(size_t index = 0; index < count; ++index) {
				hash ^= bytes[index];
				hash *= 16777619u;
			}
			return hash;
		}

		// Keeps nothing of what is written to it but a hash
		class HashBuffer : public std::streambuf {
		public:
			std::uint32_t hash = hashStart;

		protected:
			std::streamsize xsputn(const char* text, std::streamsize count) override {
				hash = hashBytes(hash, reinterpret_cast<const unsigned char*>(text), static_cast<size_t>(count));
				return count;
			}
			int_type overflow(int_type character) override {
				if(!traits_type::eq_int_type(character, traits_type::eof())) {
					const unsigned char byte = static_cast<unsigned char>(character);
					hash = hashBytes(hash, &byte, 1);
				}
				return traits_type::not_eof(character);
			}
		};

		class TextWriter {
		public:
			// Given a stream, the writer hands text over as it accumulates instead of holding all of it
			explicit TextWriter(const TextOptions& options, std::ostream* stream = nullptr)
				: options(options), stream(stream) {}

			void writeSection(const Section& section) {
				text += "section ";
				writeString(section.name);
				text += ' ';
				writeStructure(section, 0);
				text += '\n';
			}

			void writeStructure(const Structure& structure, size_t indentation) {
				if(structure.empty()) {
					text += "{}";
					return;
				}
				text += '{';
				bool first = true;
				for(const std::string& key : structure.getEntries()) {
					if(!first)
						text += ',';
					first = false;
					newline(indentation + indentStep);
					if(isBareKey(key))
						text += key;
					else
						writeString(key);
					text += ": ";
					writeValue(structure[key], indentation + indentStep);
				}
				newline(indentation);
				text += '}';
			}

			void flush() {
				stream->write(text.data(), static_cast<std::streamsize>(text.size()));
				text.clear();
			}

			std::string text;

		private:
			static constexpr size_t indentStep = 3;
			static constexpr size_t elementsPerLine = 8;   // vector elements; a longer vector is wrapped
			static constexpr size_t bytesPerLine = 32;     // blob bytes; a larger blob is wrapped
			static constexpr size_t flushSize = 1 << 16;

			static bool isBareKey(std::string_view key) {
				return !key.empty() && isWordStart(static_cast<unsigned char>(key.front())) &&
				       std::all_of(key.begin(), key.end(), [](unsigned char character) { return isWordCharacter(character); });
			}

			void newline(size_t indentation) {
				text += '\n';
				text.append(indentation, ' ');
				if(stream && text.size() >= flushSize)
					flush();
			}

			void writeValue(const Variant& variant, size_t indentation) {
				const DataType dataType = variant.getType();
				switch(dataType) {
				case DataType::structure:
					writeStructure(static_cast<const Structure&>(variant), indentation);
					break;
				case DataType::array:
					writeArray(static_cast<const Array&>(variant), indentation);
					break;
				case DataType::string:
					writeString(static_cast<std::string>(variant));
					break;
				case DataType::boolean:
					text += static_cast<bool>(variant) ? "true" : "false";
					break;
				case DataType::int32:
					writeNumber(static_cast<std::int32_t>(variant));
					break;
				case DataType::float8:
					writeBareFloat8(static_cast<double>(variant));
					break;
				case DataType::blob:
					writeBlob(variant, indentation);
					break;
				default: {
					const NumericType* type = findNumericType(dataType);
					throw_if<std::invalid_argument>(!type, "Oms: a value of an unsupported type has no text form");
					visitNumericType(type->scalar, [&]<class T>(std::type_identity<T>) {
						if(dataType == type->vector)
							writeVector<T>(*type, variant, indentation);
						else {
							text += type->name;
							text += '(';
							writeNumber(static_cast<T>(variant));
							text += ')';
						}
					});
					break;
				}
				}
			}

			void writeArray(const Array& array, size_t indentation) {
				if(array.empty()) {
					text += "[]";
					return;
				}
				if(array.size() > options.maxArrayElements) {
					text += std::format("[{} elided {:08x}]", array.size(), contentHash(array));
					return;
				}
				text += '[';
				for(size_t index = 0; index < array.size(); ++index) {
					if(index > 0)
						text += ',';
					newline(indentation + indentStep);
					writeStructure(array[index], indentation + indentStep);
				}
				newline(indentation);
				text += ']';
			}

			void writeString(std::string_view value) {
				text += '"';
				for(const char character : value) {
					switch(character) {
					case '"':
						text += "\\\"";
						break;
					case '\\':
						text += "\\\\";
						break;
					case '\n':
						text += "\\n";
						break;
					case '\r':
						text += "\\r";
						break;
					case '\t':
						text += "\\t";
						break;
					default:
						if(static_cast<unsigned char>(character) < 0x20) {
							text += "\\x";
							writeHex(reinterpret_cast<const std::uint8_t*>(&character), 1);
						} else
							text += character;
						break;
					}
				}
				text += '"';
			}

			template<class T>
			void writeNumber(T value) {
				if constexpr (std::is_floating_point_v<T>) {
					// to_chars spells these differently from one standard library to the next
					// (a NaN can come out as "-nan(ind)"), so they are written here instead
					if(std::isnan(value)) {
						text += std::signbit(value) ? "-nan" : "nan";
						return;
					}
					if(std::isinf(value)) {
						text += std::signbit(value) ? "-inf" : "inf";
						return;
					}
				}
				// For a float this is the shortest text that reads back to the same bits
				char buffer[64];
				auto [end, errorCode] = std::to_chars(buffer, buffer + sizeof(buffer), value);
				text.append(buffer, end);
			}

			void writeBareFloat8(double value) {
				const size_t start = text.size();
				writeNumber(value);
				// With neither a '.' nor an exponent it would read back as an int32
				if(std::isfinite(value) && text.find_first_of(".e", start) == std::string::npos)
					text += ".0";
			}

			template<class T>
			void writeVector(const NumericType& type, const Variant& vector, size_t indentation) {
				const T* elements = static_cast<const T*>(static_cast<void*>(vector));
				const size_t count = vector.size();
				text += type.name;
				text += '[';
				if(count > options.maxVectorElements)
					text += std::format("{} elided {:08x}", count, contentHash(elements, count));
				else if(count <= elementsPerLine) {
					for(size_t index = 0; index < count; ++index) {
						if(index > 0)
							text += ", ";
						writeNumber(elements[index]);
					}
				} else {
					for(size_t index = 0; index < count; ++index) {
						if(index % elementsPerLine == 0)
							newline(indentation + indentStep);
						else
							text += ' ';
						writeNumber(elements[index]);
						if(index + 1 < count)
							text += ',';
					}
					newline(indentation);
				}
				text += ']';
			}

			void writeBlob(const Variant& blob, size_t indentation) {
				const std::uint8_t* bytes = static_cast<const std::uint8_t*>(static_cast<void*>(blob));
				const size_t count = blob.size();
				text += "blob(";
				if(count > options.maxBlobBytes)
					text += std::format("{} bytes elided {:08x}", count, contentHash(bytes, count));
				else if(count <= bytesPerLine)
					writeHex(bytes, count);
				else {
					for(size_t offset = 0; offset < count; offset += bytesPerLine) {
						newline(indentation + indentStep);
						writeHex(bytes + offset, std::min(bytesPerLine, count - offset));
					}
					newline(indentation);
				}
				text += ')';
			}

			// Two digits per byte, with a space after every fourth byte
			void writeHex(const std::uint8_t* bytes, size_t count) {
				static constexpr char digits[] = "0123456789abcdef";
				for(size_t index = 0; index < count; ++index) {
					if(index > 0 && index % 4 == 0)
						text += ' ';
					text += digits[bytes[index] >> 4];
					text += digits[bytes[index] & 15];
				}
			}

			// Hashes the little-endian (wire) bytes, so every host prints the same hash
			template<class T>
			static std::uint32_t contentHash(const T* elements, size_t count) {
				std::uint32_t hash = hashStart;
				for(size_t index = 0; index < count; ++index) {
					unsigned char bytes[sizeof(T)];
					std::memcpy(bytes, &elements[index], sizeof(T));
					if constexpr (std::endian::native != std::endian::little)
						std::reverse(bytes, bytes + sizeof(T));
					hash = hashBytes(hash, bytes, sizeof(T));
				}
				return hash;
			}

			// Hashes the array's text as it would be written in full, with no limits and no
			// indentation, so the hash does not depend on the options or on where the array sits.
			// The text is hashed as it is produced; it is never held whole.
			static std::uint32_t contentHash(const Array& array) {
				HashBuffer buffer;
				std::ostream hashStream(&buffer);
				const TextOptions everything;
				TextWriter writer(everything, &hashStream);
				writer.writeArray(array, 0);
				writer.flush();
				return buffer.hash;
			}

			const TextOptions& options;
			std::ostream* stream;
		};

		constexpr int endOfInput = std::char_traits<char>::eof();

		// The characters of a string_view
		class ViewSource {
		public:
			explicit ViewSource(std::string_view text) : cursor(text.data()), end(text.data() + text.size()) {}

			int peek() const {
				return cursor == end ? endOfInput : static_cast<unsigned char>(*cursor);
			}
			void advance() {
				++cursor;
			}

		private:
			const char* cursor;
			const char* end;
		};

		// The characters of a stream, taken one at a time so that whatever follows the last one
		// used is still in the stream for the next read
		class StreamSource {
		public:
			explicit StreamSource(std::streambuf& buffer) : buffer(buffer), current(buffer.sgetc()) {}

			int peek() const {
				return current;
			}
			void advance() {
				current = buffer.snextc();
			}

		private:
			std::streambuf& buffer;
			int current;	// looked at, but not yet taken from the stream
		};

		// Source is ViewSource or StreamSource
		template<class Source>
		class TextParser {
		public:
			TextParser(Source source, TextPosition& position) : source(source), position(position) {}

			// "{ ... }" with nothing after it
			void parseStructure(Structure& structure) {
				skipSpace();
				expect('{', "'{'");
				parseMembers(structure);
				skipSpace();
				if(peek() != endOfInput)
					unexpected("the end of the text");
			}

			// The next "section "name" { ... }"; false if there is none
			bool parseSection(Section& section) {
				skipSpace();
				if(peek() == endOfInput)
					return false;

				const TextPosition start = position;
				if(!isWordStart(peek()))
					unexpected("'section'");
				const std::string word = readWord();
				if(word != "section")
					failIf(true, start, std::format("expected 'section', found '{}'", word));

				skipSpace();
				if(peek() != '"')
					unexpected("a quoted section name");
				const TextPosition nameStart = position;
				section.name = parseString();
				checkIdentifier(section.name, nameStart, "section name");

				skipSpace();
				expect('{', "'{'");
				parseMembers(section);
				return true;
			}

		private:
			enum class Conversion { ok, malformed, outOfRange };

			int peek() const {
				return source.peek();
			}

			int next() {
				const int character = source.peek();
				if(character == endOfInput)
					return character;
				if(character == '\n') {
					++position.line;
					position.column = 1;
				} else
					++position.column;
				source.advance();
				return character;
			}

			// Every parse failure is raised here
			void failIf(bool condition, const TextPosition& where, std::string_view message) const {
				throw_if<ParseError>(condition, where.line, where.column, message);
			}

			// Fails at the next character, naming it
			void unexpected(std::string_view wanted) const {
				failIf(true, position, std::format("expected {}, found {}", wanted, describe(peek())));
			}

			void expect(int wanted, std::string_view description) {
				if(peek() != wanted)
					unexpected(description);
				next();
			}

			static std::string describe(int character) {
				if(character == endOfInput)
					return "the end of the text";
				if(character >= 0x20 && character < 0x7f)
					return std::format("'{}'", static_cast<char>(character));
				return std::format("byte 0x{:02x}", character);
			}

			// Skips white space and comments
			void skipSpace() {
				for(;;) {
					const int character = peek();
					if(character == ' ' || character == '\t' || character == '\r' || character == '\n')
						next();
					else if(character == '/') {
						const TextPosition start = position;
						next();
						failIf(peek() != '/', start, "expected '//' to start a comment");
						while(peek() != '\n' && peek() != endOfInput)
							next();
					} else
						return;
				}
			}

			std::string readWord() {
				std::string word;
				while(isWordCharacter(peek()))
					word += static_cast<char>(next());
				return word;
			}

			std::string readNumber() {
				if(!isNumberCharacter(peek()))
					unexpected("a number");
				std::string token;
				while(isNumberCharacter(peek()))
					token += static_cast<char>(next());
				return token;
			}

			// Called with the opening quote next
			std::string parseString() {
				const TextPosition start = position;
				next();
				std::string value;
				for(;;) {
					const TextPosition where = position;
					const int character = next();
					if(character == '"')
						return value;
					// A string closes on the line it opens on; the writer escapes line breaks
					failIf(character == endOfInput || character == '\n' || character == '\r', start, "unterminated string");
					if(character != '\\') {
						value += static_cast<char>(character);
						continue;
					}
					switch(next()) {
					case '"':
						value += '"';
						break;
					case '\\':
						value += '\\';
						break;
					case 'n':
						value += '\n';
						break;
					case 'r':
						value += '\r';
						break;
					case 't':
						value += '\t';
						break;
					case 'x': {
						const int high = hexValue(next());
						const int low = hexValue(next());
						failIf(high < 0 || low < 0, where, "expected two hexadecimal digits after \\x");
						value += static_cast<char>(high * 16 + low);
						break;
					}
					default:
						failIf(true, where, "unknown escape; the escapes are \\\" \\\\ \\n \\r \\t and \\xHH");
						break;
					}
				}
			}

			// A key or a section name has to fit the binary form's identifier
			void checkIdentifier(const std::string& identifier, const TextPosition& where, std::string_view kind) const {
				if(identifier.size() > maxIdentifierSize || identifier.find('\0') != std::string::npos)
					failIf(true, where, std::format("a {} is limited to {} bytes and cannot contain a NUL", kind, maxIdentifierSize));
			}

			// Called just after the opening brace; reads through the closing one
			void parseMembers(Structure& structure) {
				for(;;) {
					skipSpace();
					if(peek() == '}') {
						next();
						return;
					}

					const TextPosition keyStart = position;
					std::string key;
					if(peek() == '"')
						key = parseString();
					else {
						if(!isWordStart(peek()))
							unexpected("a key or '}'");
						key = readWord();
					}
					checkIdentifier(key, keyStart, "key");
					if(structure.contains(key))
						failIf(true, keyStart, std::format("repeated key \"{}\"", key));

					skipSpace();
					expect(':', "':' after the key");
					skipSpace();
					parseValue(structure, key);

					skipSpace();
					if(peek() == ',')
						next();
					else if(peek() != '}')
						unexpected("',' or '}'");
				}
			}

			// Called just after the opening bracket, which was at `start`; reads through the closing one
			void parseArray(Array& array, const TextPosition& start) {
				for(;;) {
					skipSpace();
					if(peek() == ']') {
						next();
						return;
					}
					if(peek() != '{') {
						const TextPosition where = position;
						const std::string message = std::format("expected '{{' (a bare '[' holds structures; a vector is "
							"written with its element type, as in int32[1, 2]), found {}", describe(peek()));
						// Summary mode writes "[2008331 elided 9f3c2a1b]" in place of the structures
						if(isNumberCharacter(peek())) {
							readNumber();
							skipSpace();
							failIf(isWordStart(peek()) && readWord() == "elided", start, elidedMessage);
						}
						failIf(true, where, message);
					}
					next();
					parseMembers(array.addStructure());

					skipSpace();
					if(peek() == ',')
						next();
					else if(peek() != ']')
						unexpected("',' or ']'");
				}
			}

			void parseValue(Structure& structure, const std::string& key) {
				const TextPosition start = position;
				const int character = peek();
				if(character == '{') {
					next();
					parseMembers(structure.addStructure(key));
				} else if(character == '[') {
					next();
					parseArray(structure.addArray(key), start);
				} else if(character == '"')
					structure.add(key, parseString());
				else if(isWordStart(character)) {
					const std::string word = readWord();
					if(word == "true" || word == "false")
						structure.add(key, word == "true");
					else if(word == "nan" || word == "inf")
						structure.add(key, toNumber<double>(word, start, "float8"));
					else if(word == "blob")
						parseBlob(structure, key, start);
					else
						parseTyped(structure, key, word, start);
				} else if(isNumberCharacter(character)) {
					// A bare number is an int32 unless it has a '.' or an exponent, which makes it a float8
					const std::string token = readNumber();
					if(isInteger(token))
						structure.add(key, toNumber<std::int32_t>(token, start, "int32"));
					else
						structure.add(key, toNumber<double>(token, start, "float8"));
				} else
					unexpected("a value");
			}

			// "uint8(200)" or "int16[180, 94]", called just after the type name
			void parseTyped(Structure& structure, const std::string& key, const std::string& typeName, const TextPosition& start) {
				const NumericType* type = findNumericType(typeName);
				if(!type)
					failIf(true, start, std::format("unknown type name '{}'", typeName));

				skipSpace();
				if(peek() == '(') {
					next();
					skipSpace();
					const TextPosition where = position;
					const std::string token = readNumber();
					visitNumericType(type->scalar, [&]<class T>(std::type_identity<T>) {
						structure.add(key, toNumber<T>(token, where, type->name));
					});
					skipSpace();
					expect(')', "')'");
				} else if(peek() == '[') {
					next();
					visitNumericType(type->scalar, [&]<class T>(std::type_identity<T>) {
						parseVector<T>(structure, key, *type, start);
					});
				} else
					unexpected("'(' or '[' after the type name");
			}

			// Called just after the opening bracket; reads through the closing one
			template<class T>
			void parseVector(Structure& structure, const std::string& key, const NumericType& type, const TextPosition& start) {
				std::vector<T> elements;
				for(;;) {
					skipSpace();
					if(peek() == ']') {
						next();
						break;
					}
					const TextPosition where = position;
					const std::string token = readNumber();

					// Summary mode writes "float4[4096 elided 9f3c2a1b]" in place of the elements
					skipSpace();
					if(isWordStart(peek())) {
						const TextPosition wordStart = position;
						const std::string word = readWord();
						failIf(word == "elided", start, elidedMessage);
						failIf(true, wordStart, std::format("expected ',' or ']', found '{}'", word));
					}
					elements.push_back(toNumber<T>(token, where, type.name));

					if(peek() == ',')
						next();
					else if(peek() != ']')
						unexpected("',' or ']'");
				}
				structure.addVector<T>(key, elements.data(), elements.size());
			}

			// Called just after "blob"
			void parseBlob(Structure& structure, const std::string& key, const TextPosition& start) {
				skipSpace();
				expect('(', "'(' after blob");
				std::vector<std::uint8_t> bytes;
				for(;;) {
					skipSpace();
					if(peek() == ')') {
						next();
						break;
					}
					const TextPosition where = position;
					const int high = hexValue(peek());
					if(high >= 0)
						next();
					const int low = high < 0 ? -1 : hexValue(peek());
					if(low < 0) {
						const std::string message = high < 0
							? std::format("expected hexadecimal digits or ')', found {}", describe(peek()))
							: std::string("expected two hexadecimal digits per byte");
						// Summary mode writes "blob(131072 bytes elided 9f3c2a1b)" in place of the bytes
						failIf(restOfBlobSaysElided(), start, elidedMessage);
						failIf(true, where, message);
					}
					next();
					bytes.push_back(static_cast<std::uint8_t>(high * 16 + low));
				}
				structure.add(key, bytes.data(), bytes.size());
			}

			// Only called once a blob has turned out to be malformed, so reading ahead costs nothing
			bool restOfBlobSaysElided() {
				std::string rest;
				while(rest.size() < 64 && peek() != ')' && peek() != endOfInput)
					rest += static_cast<char>(next());
				return rest.find("elided") != std::string::npos;
			}

			static bool isInteger(const std::string& token) {
				const size_t digits = token.starts_with('-') ? 1 : 0;
				return token.size() > digits && std::all_of(token.begin() + digits, token.end(), [](unsigned char character) { return isDigit(character); });
			}

			template<class T>
			T toNumber(const std::string& token, const TextPosition& where, std::string_view typeName) const {
				T value{};
				const Conversion conversion = convert(token, value);
				// The message is only put together on failure; this runs once per vector element
				if(conversion == Conversion::outOfRange)
					failIf(true, where, std::format("{} does not fit {}", token, typeName));
				else if(conversion == Conversion::malformed)
					failIf(true, where, std::format("expected {}, found '{}'", std::is_integral_v<T> ? "an integer" : "a number", token));
				return value;
			}

			template<class T>
			static Conversion convert(const std::string& token, T& value) {
				const char* first = token.data();
				const char* last = first + token.size();
				if constexpr (std::is_integral_v<T>) {
					if(!isInteger(token))
						return Conversion::malformed;
					// from_chars turns down a '-' for an unsigned type, which is a negative number not fitting
					const auto [end, errorCode] = std::from_chars(first, last, value);
					return errorCode == std::errc() && end == last ? Conversion::ok : Conversion::outOfRange;
				} else {
					if(token == "nan" || token == "-nan") {
						value = std::copysign(std::numeric_limits<T>::quiet_NaN(), token.front() == '-' ? T(-1) : T(1));
						return Conversion::ok;
					}
					if(token == "inf" || token == "-inf") {
						value = std::copysign(std::numeric_limits<T>::infinity(), token.front() == '-' ? T(-1) : T(1));
						return Conversion::ok;
					}
					// Checked here so that the text has one spelling of each number whichever
					// conversion is used below: no "infinity", no hexadecimal, no leading '+'
					if(token.find_first_not_of("0123456789.eE+-") != std::string::npos || token.front() == '+')
						return Conversion::malformed;

					if constexpr (requires { std::from_chars(first, last, value); }) {
						const auto [end, errorCode] = std::from_chars(first, last, value);
						if(end != last)
							return Conversion::malformed;
						if(errorCode == std::errc::result_out_of_range)
							return Conversion::outOfRange;
						return errorCode == std::errc() ? Conversion::ok : Conversion::malformed;
					} else {
						// Some standard libraries (libc++ before LLVM 20) have no floating-point from_chars.
						// strtod stands in for it; unlike from_chars it follows the C locale's decimal point.
						char* end = nullptr;
						errno = 0;
						if constexpr (std::is_same_v<T, float>)
							value = std::strtof(first, &end);
						else
							value = std::strtod(first, &end);
						if(end != last)
							return Conversion::malformed;
						// ERANGE on its own is not a failure: strtod also sets it for a denormal, which is a valid value
						if(errno == ERANGE && (value == 0 || std::isinf(value)))
							return Conversion::outOfRange;
						return Conversion::ok;
					}
				}
			}

			static constexpr std::string_view elidedMessage =
				"this value was elided by summary mode, so the text does not hold its contents";

			Source source;
			TextPosition& position;
		};
	}
	/// @endcond

	/**
	 * @brief Writes a Structure as text of the form <tt>{ key: value, ... }</tt>.
	 *
	 * The text is the same every time for the same data: one member per line, nested
	 * structures indented, no line break at the end. With the default options it reads
	 * back through @c fromText() to an identical Structure.
	 *
	 * @param structure  The Structure (or Section, whose name is not written) to convert.
	 * @param options    Limits beyond which vectors and blobs are elided; none by default.
	 * @return           The text.
	 * @throws std::invalid_argument if a member has an unsupported DataType.
	 */
	inline std::string toText(const Structure& structure, const TextOptions& options = {}) {
		internal::TextWriter writer(options);
		writer.writeStructure(structure, 0);
		return std::move(writer.text);
	}

	/**
	 * @brief Reads a Structure from text of the form <tt>{ key: value, ... }</tt>.
	 *
	 * The text holds exactly one structure, which is what a hand-written file would
	 * normally be. Numbers written without a type become @c int32 and @c float8;
	 * since the numeric conversions of Variant apply as usual, <tt>getOr("gain", 1.0f)</tt>
	 * reads a value that was written as @c 0.5.
	 *
	 * @param text  The text to read.
	 * @return      The Structure.
	 * @throws ParseError if the text is malformed, a number does not fit its type,
	 *         a key is repeated, or a value was elided when the text was written.
	 */
	inline Structure fromText(std::string_view text) {
		TextPosition position;
		Structure structure;
		internal::TextParser(internal::ViewSource(text), position).parseStructure(structure);
		return structure;
	}

	/**
	 * @brief Writes a Section as text of the form <tt>section "name" { ... }</tt>, ending with a line break.
	 *
	 * Call it once per section to write a whole file as text. Unlike the binary
	 * @c operator<<, it leaves the Section as it was.
	 *
	 * @param ostream  Destination stream.
	 * @param section  The Section to write.
	 * @param options  Limits beyond which vectors and blobs are elided; none by default.
	 * @throws std::invalid_argument if a member has an unsupported DataType, or if the
	 *         Section was read from a file that uses one of the reserved header fields,
	 *         which text has no place for.
	 */
	inline void writeText(std::ostream& ostream, const Section& section, const TextOptions& options = {}) {
		throw_if<std::invalid_argument>(internal::SectionHeader::usesReservedFields(section),
			std::format("Oms: section \"{}\" uses a reserved header field, which text cannot hold", section.name));
		internal::TextWriter writer(options, &ostream);
		writer.writeSection(section);
		writer.flush();
	}

	/**
	 * @brief Reads the next <tt>section "name" { ... }</tt> from a stream of text.
	 *
	 * Reads no further than the section's closing brace, so calling it repeatedly reads
	 * the sections of a file in turn:
	 * @code
	 * oms::TextPosition position;
	 * oms::Section section;
	 * while(oms::readText(in, section, position))
	 *     out << section;
	 * @endcode
	 *
	 * @param istream   Source stream.
	 * @param section   Receives the section; whatever it held before is cleared.
	 * @param position  Where reading has got to. Pass the same object to every call on one
	 *                  stream so that a ParseError gives its place in the whole text.
	 * @return          @c false if no section is left, @c true otherwise.
	 * @throws ParseError as @c fromText() does.
	 */
	inline bool readText(std::istream& istream, Section& section, TextPosition& position) {
		section.clear();
		std::streambuf* buffer = istream.rdbuf();
		return buffer && internal::TextParser(internal::StreamSource(*buffer), position).parseSection(section);
	}

	/**
	 * @brief Reads the next <tt>section "name" { ... }</tt> from a stream of text.
	 *
	 * Does what the overload taking a TextPosition does, except that a ParseError gives its
	 * place counted from where this call began reading. That is the place in the whole text
	 * only for the first section.
	 */
	inline bool readText(std::istream& istream, Section& section) {
		TextPosition position;
		return readText(istream, section, position);
	}
}
