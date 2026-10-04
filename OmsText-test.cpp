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

#include <cmath>
#include <sstream>
#include <format>
#include <catch2/catch_test_macros.hpp>
#include "Oms.hpp"
#include "OmsText.hpp"

// Defined in Oms-test.cpp
void randomlyPopulateStructure(oms::Structure& structure, int oddsOfArray = 1);

namespace {
	// The binary form of a Structure. Equal bytes mean the same members in the same order, each
	// with the same DataType and the same value bit for bit. That is stricter than operator==,
	// which takes members in any order and compares values after converting between types.
	std::string binaryOf(const oms::Structure& structure) {
		oms::Structure copy(structure);	// serializing takes a non-const Structure
		std::stringstream stream;
		stream << copy;
		return stream.str();
	}

	// Structure -> text -> Structure gives back the same thing, and gives the same text again
	void checkRoundTrip(const oms::Structure& structure) {
		const std::string text = oms::toText(structure);
		INFO(text);
		const oms::Structure reread = oms::fromText(text);
		CHECK((binaryOf(reread) == binaryOf(structure)));
		CHECK(oms::toText(reread) == text);
	}

	// Reading `text` fails at the given place, with a message that includes `reason`
	void checkParseError(const std::string& text, size_t line, size_t column, const std::string& reason) {
		INFO(text);
		try {
			oms::fromText(text);
			FAIL_CHECK("no ParseError was thrown");
		} catch(const oms::ParseError& error) {
			const std::string what = error.what();
			INFO(what);
			CHECK(error.line == line);
			CHECK(error.column == column);
			CHECK(what.starts_with(std::format("{}:{}: ", line, column)));
			CHECK(what.find(reason) != std::string::npos);
		}
	}

	template<class T>
	void addIntegers(oms::Structure& structure, const std::string& name) {
		structure.add(name + ".min", std::numeric_limits<T>::min());
		structure.add(name + ".max", std::numeric_limits<T>::max());
		structure.add(name + ".zero", T{0});
		structure.add(name + ".one", T{1});
	}

	template<class T>
	void addFloats(oms::Structure& structure, const std::string& name) {
		structure.add(name + ".lowest", std::numeric_limits<T>::lowest());
		structure.add(name + ".max", std::numeric_limits<T>::max());
		structure.add(name + ".min", std::numeric_limits<T>::min());
		structure.add(name + ".denormal", std::numeric_limits<T>::denorm_min());
		structure.add(name + ".bigDenormal", std::numeric_limits<T>::min() / 2);
		structure.add(name + ".epsilon", std::numeric_limits<T>::epsilon());
		structure.add(name + ".zero", T{0});
		structure.add(name + ".minusZero", -T{0});
		structure.add(name + ".inf", std::numeric_limits<T>::infinity());
		structure.add(name + ".minusInf", -std::numeric_limits<T>::infinity());
		structure.add(name + ".third", T{1} / 3);
		structure.add(name + ".tenth", T{1} / 10);
		structure.add(name + ".whole", T{39});
		structure.add(name + ".large", T{1e20f});
		structure.add(name + ".small", T{-1e-20f});
	}

	template<class T>
	void addVectors(oms::Structure& structure, const std::string& name) {
		structure.addVector<T>(name + ".empty");
		// 8 elements still fit on one line; 9 and more are wrapped
		for(size_t count : {1, 2, 8, 9, 16, 17, 100}) {
			std::vector<T> values;
			for(size_t index = 0; index < count; ++index)
				values.push_back(index == 0 ? std::numeric_limits<T>::lowest() :
				                 index == 1 ? std::numeric_limits<T>::max() :
				                              static_cast<T>(index));
			structure.addVector(std::format("{}.{}", name, count), values);
		}
	}

	// 32-bit FNV-1a, written out again here so that the hashes in a summary are checked
	// against something other than the code that made them
	std::uint32_t fnv1a(std::string_view text) {
		std::uint32_t hash = 2166136261u;
		for(const unsigned char byte : text) {
			hash ^= byte;
			hash *= 16777619u;
		}
		return hash;
	}

	// The text of the array that is the only member of `structure`, as it would be written
	// on its own: taken from the structure's text, less the one level of indentation
	std::string loneArrayText(const oms::Structure& structure) {
		const std::string text = oms::toText(structure);
		const size_t open = text.find('[');
		const size_t close = text.rfind(']');
		std::string array;
		for(size_t at = open; at <= close; ++at) {
			array += text[at];
			if(text[at] == '\n')
				at += 3;
		}
		return array;
	}

	template<class T>
	T fromBits(auto bits) {
		static_assert(sizeof(T) == sizeof(bits));
		T value;
		std::memcpy(&value, &bits, sizeof(T));
		return value;
	}
}

TEST_CASE("OmsText format") {
	oms::Structure structure;
	structure.add("splitPoint", 240);
	oms::Structure& component = structure.addArray("components").addStructure();
	component.add("class", "Resynthesizer");
	component.add("visible", true);
	oms::Structure& placement = component.addStructure("nodePlacement");
	placement.add("x", 1051);
	placement.add("y", 172);
	component.add("redAmplification", 39.0f);
	component.add("scrollOrigin.x", 0);
	component.add("real time", 1723312000.25);
	component.add("ratio", 2.0);
	component.add("level", std::uint8_t{200});
	component.add("offset", std::int64_t{-5});
	component.addVector<std::int16_t>("point", {180, 94});
	component.addVector<float>("window", {0, 0.25f, 0.5f, 0.75f, 1, 0.75f, 0.5f, 0.25f, 0});
	const float signal[] = {1.0f, 0.5f};
	component.add("signal", signal, sizeof(signal));
	std::vector<std::uint8_t> table(38);
	for(size_t index = 0; index < table.size(); ++index)
		table[index] = static_cast<std::uint8_t>(index * 7);
	component.add("table", table.data(), table.size());
	component.addStructure("options");
	structure.addArray("wires");

	// One member per line, in insertion order. Only int32, float8, boolean and string are
	// written without a type. A long vector or blob is wrapped at a fixed width.
	const std::string expected =
		"{\n"
		"   splitPoint: 240,\n"
		"   components: [\n"
		"      {\n"
		"         class: \"Resynthesizer\",\n"
		"         visible: true,\n"
		"         nodePlacement: {\n"
		"            x: 1051,\n"
		"            y: 172\n"
		"         },\n"
		"         redAmplification: float4(39),\n"
		"         scrollOrigin.x: 0,\n"
		"         \"real time\": 1723312000.25,\n"
		"         ratio: 2.0,\n"
		"         level: uint8(200),\n"
		"         offset: int64(-5),\n"
		"         point: int16[180, 94],\n"
		"         window: float4[\n"
		"            0, 0.25, 0.5, 0.75, 1, 0.75, 0.5, 0.25,\n"
		"            0\n"
		"         ],\n"
		"         signal: blob(0000803f 0000003f),\n"
		"         table: blob(\n"
		"            00070e15 1c232a31 383f464d 545b6269 70777e85 8c939aa1 a8afb6bd c4cbd2d9\n"
		"            e0e7eef5 fc03\n"
		"         ),\n"
		"         options: {}\n"
		"      }\n"
		"   ],\n"
		"   wires: []\n"
		"}";
	CHECK(oms::toText(structure) == expected);
	checkRoundTrip(structure);

	// The same data as a Section
	oms::Section section;
	section.name = "model";
	section.add("splitPoint", 240);
	std::stringstream stream;
	oms::writeText(stream, section);
	CHECK(stream.str() == "section \"model\" {\n   splitPoint: 240\n}\n");
	CHECK(section.name == "model");		// unlike operator<<, writeText leaves the Section alone
	CHECK(section.size() == 1);
}

TEST_CASE("OmsText scalars") {
	oms::Structure structure;
	addIntegers<std::uint8_t>(structure, "uint8");
	addIntegers<std::uint16_t>(structure, "uint16");
	addIntegers<std::uint32_t>(structure, "uint32");
	addIntegers<std::uint64_t>(structure, "uint64");
	addIntegers<std::int8_t>(structure, "int8");
	addIntegers<std::int16_t>(structure, "int16");
	addIntegers<std::int32_t>(structure, "int32");
	addIntegers<std::int64_t>(structure, "int64");
	addFloats<float>(structure, "float4");
	addFloats<double>(structure, "float8");
	structure.add("yes", true);
	structure.add("no", false);
	structure.add("character", 'a');	// a char is stored as an int8
	structure.add("text", std::string("hello"));

	checkRoundTrip(structure);
	CHECK(oms::fromText(oms::toText(structure)) == structure);

	// How the edge cases are spelled
	oms::Structure spelling;
	spelling.add("a", std::int8_t{-128});
	spelling.add("b", std::uint8_t{255});
	spelling.add("c", std::numeric_limits<std::int32_t>::min());
	spelling.add("d", std::numeric_limits<std::uint64_t>::max());
	spelling.add("e", 1.0);
	spelling.add("f", -0.0);
	spelling.add("g", 100000.0);
	spelling.add("h", 0.1);
	spelling.add("i", std::numeric_limits<double>::denorm_min());
	spelling.add("j", std::numeric_limits<double>::infinity());
	spelling.add("k", -std::numeric_limits<double>::infinity());
	spelling.add("l", 0.1f);
	spelling.add("m", -0.0f);
	spelling.add("n", -std::numeric_limits<float>::infinity());
	spelling.add("o", 'a');
	CHECK(oms::toText(spelling) ==
		"{\n"
		"   a: int8(-128),\n"
		"   b: uint8(255),\n"
		"   c: -2147483648,\n"
		"   d: uint64(18446744073709551615),\n"
		"   e: 1.0,\n"
		"   f: -0.0,\n"
		"   g: 1e+05,\n"
		"   h: 0.1,\n"
		"   i: 5e-324,\n"
		"   j: inf,\n"
		"   k: -inf,\n"
		"   l: float4(0.1),\n"
		"   m: float4(-0),\n"
		"   n: float4(-inf),\n"
		"   o: int8(97)\n"
		"}");
}

TEST_CASE("OmsText NaN") {
	const double nan8 = std::numeric_limits<double>::quiet_NaN();
	const float nan4 = std::numeric_limits<float>::quiet_NaN();

	oms::Structure structure;
	structure.add("d", nan8);
	structure.add("minusD", std::copysign(nan8, -1.0));
	structure.add("f", nan4);
	structure.add("minusF", std::copysign(nan4, -1.0f));
	structure.addVector<double>("dv", {nan8, 1.0, std::copysign(nan8, -1.0)});
	structure.addVector<float>("fv", {nan4, 1.0f, std::copysign(nan4, -1.0f)});
	CHECK(oms::toText(structure) ==
		"{\n"
		"   d: nan,\n"
		"   minusD: -nan,\n"
		"   f: float4(nan),\n"
		"   minusF: float4(-nan),\n"
		"   dv: float8[nan, 1, -nan],\n"
		"   fv: float4[nan, 1, -nan]\n"
		"}");

	// The quiet NaNs above come back with the same bits. (operator== is no use here: a NaN
	// is not equal to itself.)
	checkRoundTrip(structure);

	// The one thing text does not carry is a NaN's payload: any NaN is written as nan or -nan
	// and reads back as the quiet NaN of that sign.
	oms::Structure payloads;
	payloads.add("quiet", fromBits<double>(std::uint64_t{0x7ff8000000000123}));
	payloads.add("signalling", fromBits<double>(std::uint64_t{0xfff0000000000001}));
	payloads.add("single", fromBits<float>(std::uint32_t{0x7fc00456}));
	CHECK(oms::toText(payloads) == "{\n   quiet: nan,\n   signalling: -nan,\n   single: float4(nan)\n}");
	const oms::Structure reread = oms::fromText(oms::toText(payloads));
	CHECK(std::isnan(static_cast<double>(reread["quiet"])));
	CHECK(!std::signbit(static_cast<double>(reread["quiet"])));
	CHECK(std::isnan(static_cast<double>(reread["signalling"])));
	CHECK(std::signbit(static_cast<double>(reread["signalling"])));
	CHECK(reread["single"].getType() == oms::DataType::float4);
	CHECK(std::isnan(static_cast<float>(reread["single"])));
}

TEST_CASE("OmsText vectors") {
	oms::Structure structure;
	addVectors<std::uint8_t>(structure, "uint8");
	addVectors<std::uint16_t>(structure, "uint16");
	addVectors<std::uint32_t>(structure, "uint32");
	addVectors<std::uint64_t>(structure, "uint64");
	addVectors<std::int8_t>(structure, "int8");
	addVectors<std::int16_t>(structure, "int16");
	addVectors<std::int32_t>(structure, "int32");
	addVectors<std::int64_t>(structure, "int64");
	addVectors<float>(structure, "float4");
	addVectors<double>(structure, "float8");
	structure.addVector<float>("fractions", {0.1f, -0.0f, 1e-45f, 3.4028235e38f, std::numeric_limits<float>::infinity()});
	structure.addVector<double>("doubles", {0.1, -0.0, 5e-324, 1.7976931348623157e308, -std::numeric_limits<double>::infinity()});

	checkRoundTrip(structure);
	const oms::Structure reread = oms::fromText(oms::toText(structure));
	CHECK(reread == structure);
	CHECK(reread["uint8.empty"].getType() == oms::DataType::uint8v);
	CHECK(reread["uint8.empty"].size() == 0);
	CHECK(reread["float8.100"].getType() == oms::DataType::float8v);
	CHECK(reread["float8.100"].size() == 100);

	oms::Structure spelling;
	spelling.addVector<float>("empty");
	spelling.addVector<std::uint8_t>("bytes", {0, 255});
	spelling.addVector<std::int32_t>("typed", {1, -2});	// a vector always names its type, int32 included
	spelling.addVector<double>("doubles", {39, 0.5});	// and its floats need no '.'
	CHECK(oms::toText(spelling) ==
		"{\n"
		"   empty: float4[],\n"
		"   bytes: uint8[0, 255],\n"
		"   typed: int32[1, -2],\n"
		"   doubles: float8[39, 0.5]\n"
		"}");
}

TEST_CASE("OmsText blobs") {
	std::vector<std::uint8_t> bytes(1000);
	for(size_t index = 0; index < bytes.size(); ++index)
		bytes[index] = static_cast<std::uint8_t>(index * 37 + 11);

	oms::Structure structure;
	// 6 and 33 are not multiples of four; 32 still fits on one line and 33 is wrapped
	for(size_t size : {0, 1, 4, 6, 32, 33, 64, 1000})
		structure.add(std::format("blob{}", size), bytes.data(), size);

	checkRoundTrip(structure);
	const oms::Structure reread = oms::fromText(oms::toText(structure));
	CHECK(reread == structure);
	CHECK(reread["blob0"].getType() == oms::DataType::blob);
	CHECK(reread["blob0"].size() == 0);
	CHECK(reread["blob1000"].size() == 1000);
	CHECK(!memcmp(bytes.data(), reread["blob1000"], 1000));

	oms::Structure spelling;
	spelling.add("none", bytes.data(), 0);
	spelling.add("one", bytes.data(), 1);
	spelling.add("six", bytes.data(), 6);
	CHECK(oms::toText(spelling) == "{\n   none: blob(),\n   one: blob(0b),\n   six: blob(0b30557a 9fc4)\n}");
}

TEST_CASE("OmsText nesting") {
	oms::Structure structure;
	structure.addStructure("emptyStructure");
	structure.addArray("emptyArray");
	oms::Array& array = structure.addArray("array");
	array.addStructure();
	array.addStructure().add("x", 1);
	oms::Structure& element = array.addStructure();
	element.addArray("inner").addStructure().addStructure("deep").addArray("deeper").addStructure().add("y", 2.5);
	element.addStructure("sibling").addStructure("child").add("z", "text");

	checkRoundTrip(structure);
	const oms::Structure reread = oms::fromText(oms::toText(structure));
	CHECK(reread == structure);
	CHECK(reread["emptyStructure"].getType() == oms::DataType::structure);
	CHECK(reread["emptyStructure"].size() == 0);
	CHECK(reread["emptyArray"].getType() == oms::DataType::array);
	CHECK(reread["emptyArray"].size() == 0);

	// Array elements know their place, as they do when read from binary
	const oms::Array& rereadArray = static_cast<const oms::Array&>(reread["array"]);
	REQUIRE(rereadArray.size() == 3);
	CHECK(rereadArray[0].empty());
	CHECK(rereadArray[1].index == 1);
	CHECK(rereadArray[2].index == 2);
	CHECK(rereadArray[2]["sibling"]["child"]["z"] == std::string("text"));
	CHECK(!reread.index.has_value());
}

TEST_CASE("OmsText keys") {
	const std::vector<std::string> keys = {
		"plain", "_under_score9", "dotted.name", "trailing.",
		// Written quoted
		"with space", "quote\"inside", "back\\slash", "new\nline", "tab\tbed", "na\xc3\xafve \xd0\xba\xd0\xbb\xd1\x8e\xd1\x87 \xe9\x8d\xb5",
		"", "123", "9lives", ".leading", "hy-phen", "a:b", "{", "//",
		// Words that mean something elsewhere are ordinary keys
		"true", "nan", "section", "blob", "int32",
		std::string(255, 'k'), std::string(255, ' ')
	};

	oms::Structure structure;
	int value = 0;
	for(const std::string& key : keys)
		structure.add(key, value++);
	REQUIRE(structure.size() == keys.size());

	checkRoundTrip(structure);
	const std::string text = oms::toText(structure);
	const oms::Structure reread = oms::fromText(text);
	CHECK(reread == structure);
	CHECK(reread.getEntries() == keys);

	CHECK(text.find("\n   dotted.name: 2,") != std::string::npos);
	CHECK(text.find("\n   trailing.: 3,") != std::string::npos);
	CHECK(text.find("\n   \"with space\": 4,") != std::string::npos);
	CHECK(text.find("\n   \"quote\\\"inside\": 5,") != std::string::npos);
	CHECK(text.find("\n   \"\": 10,") != std::string::npos);
	CHECK(text.find("\n   \"123\": 11,") != std::string::npos);
	CHECK(text.find("\n   \".leading\": 13,") != std::string::npos);
	CHECK(text.find("\n   true: 18,") != std::string::npos);

	// The binary form has no room for a longer key, or for a NUL in one
	checkParseError("{\n   \"" + std::string(256, 'k') + "\": 1\n}", 2, 4, "limited to 255 bytes");
	checkParseError("{ " + std::string(256, 'k') + ": 1 }", 1, 3, "limited to 255 bytes");
	checkParseError("{ \"a\\x00b\": 1 }", 1, 3, "cannot contain a NUL");
}

TEST_CASE("OmsText strings") {
	const std::vector<std::string> strings = {
		"", "plain", "say \"hi\"", "C:\\path\\file", "\\\"", "line one\nline two\r\n", "\ttabbed\t",
		"na\xc3\xafve \xd0\xba\xd0\xbb\xd1\x8e\xd1\x87 \xe9\x8d\xb5 \xf0\x9f\x8e\xb5",
		std::string("\x01\x02\x1f\x7f", 4), std::string("nul\0inside", 10), std::string(1, '\0'),
		"// not a comment", "{ not: \"a structure\" }", std::string(5000, 'x'),
		"\xff\xfe not valid UTF-8 \x80"
	};

	oms::Structure structure;
	for(size_t index = 0; index < strings.size(); ++index)
		structure.add(std::format("s{}", index), strings[index]);

	checkRoundTrip(structure);
	const oms::Structure reread = oms::fromText(oms::toText(structure));
	CHECK(reread == structure);
	for(size_t index = 0; index < strings.size(); ++index)
		CHECK(static_cast<std::string>(reread[std::format("s{}", index)]) == strings[index]);

	// The escapes are \" \\ \n \r \t, and \xHH for the other bytes below 0x20. Everything else,
	// UTF-8 included, is written as it is.
	oms::Structure spelling;
	spelling.add("s", std::string("a\"b\\c\nd\re\tf\x01g\x1fh\0i\x7f\xc3\xaf", 20));
	CHECK(oms::toText(spelling) == "{\n   s: \"a\\\"b\\\\c\\nd\\re\\tf\\x01g\\x1fh\\x00i\x7f\xc3\xaf\"\n}");

	// \xHH is read for any byte, in either case
	CHECK(static_cast<std::string>(oms::fromText("{ s: \"\\x41\\x7A\\xC3\\xaf\" }")["s"]) == "Az\xc3\xaf");
}

TEST_CASE("OmsText sections") {
	std::stringstream text;
	std::stringstream binary;

	oms::Section section;
	section.name = "config";
	section.add("version", std::uint32_t{1});
	section.addVector<float>("weights", {1.0f, 2.0f, 3.0f});
	oms::writeText(text, section);
	binary << section;	// clears the section

	section.name = "section with a space, a \"quote\" and \xc3\xa9";
	section.addArray("rows").addStructure().add("score", 0.95);
	oms::writeText(text, section);
	binary << section;

	section.name = "";
	oms::writeText(text, section);
	binary << section;

	section.name = "last";
	randomlyPopulateStructure(section);
	oms::writeText(text, section);
	binary << section;

	// Text back to binary gives the same bytes, section headers and sizes included
	std::stringstream binaryFromText;
	std::vector<std::string> names;
	oms::TextPosition position;
	oms::Section reread;
	while(oms::readText(text, reread, position)) {
		names.push_back(reread.name);
		binaryFromText << reread;
	}
	CHECK(names == std::vector<std::string>{"config", "section with a space, a \"quote\" and \xc3\xa9", "", "last"});
	CHECK((binaryFromText.str() == binary.str()));

	// At the end of the input there is nothing more to read, however often it is asked for
	CHECK(!oms::readText(text, reread, position));
	CHECK(!oms::readText(text, reread));
	CHECK(reread.empty());

	// And the binary read back the usual way gives the same text
	std::stringstream textFromBinary;
	while(binaryFromText.peek() != std::stringstream::traits_type::eof()) {
		binaryFromText >> reread;
		oms::writeText(textFromBinary, reread);
	}
	CHECK(textFromBinary.str() == text.str());
}

TEST_CASE("OmsText reading sections") {
	// A section is read up to its closing brace and no further, whatever the layout
	std::stringstream text(
		"// A file may start with a comment\n"
		"section \"a\" { x: 1 } section\n"
		"   \"b\"\n"
		"{\n"
		"   y: 2,   // and have them anywhere\n"
		"}\n"
		"\n"
		"// and end with one");
	oms::Section section;
	REQUIRE(oms::readText(text, section));
	CHECK(section.name == "a");
	CHECK(section["x"] == 1);
	REQUIRE(oms::readText(text, section));
	CHECK(section.name == "b");
	CHECK(section.size() == 1);	// what the first read left in the section is gone
	CHECK(section["y"] == 2);
	CHECK(!oms::readText(text, section));

	// Given a TextPosition, an error is placed within the whole text
	const std::string broken =
		"section \"a\" {\n"
		"   x: 1\n"
		"}\n"
		"section \"b\" {\n"
		"   y: uint8(300)\n"
		"}\n";
	std::stringstream tracked(broken);
	oms::TextPosition position;
	REQUIRE(oms::readText(tracked, section, position));
	try {
		oms::readText(tracked, section, position);
		FAIL_CHECK("no ParseError was thrown");
	} catch(const oms::ParseError& error) {
		CHECK(error.line == 5);
		CHECK(error.column == 13);
		CHECK(std::string(error.what()) == "5:13: 300 does not fit uint8");
	}

	// Without one, it is counted from where the failing read started
	std::stringstream untracked(broken);
	REQUIRE(oms::readText(untracked, section));
	try {
		oms::readText(untracked, section);
		FAIL_CHECK("no ParseError was thrown");
	} catch(const oms::ParseError& error) {
		CHECK(error.line == 3);
		CHECK(error.column == 13);
	}

	// A file is made of sections, and a lone structure is not one
	std::stringstream bare("{ x: 1 }");
	CHECK_THROWS_AS(oms::readText(bare, section), oms::ParseError);
	std::stringstream unnamed("section { x: 1 }");
	CHECK_THROWS_AS(oms::readText(unnamed, section), oms::ParseError);
	std::stringstream longName("section \"" + std::string(256, 'n') + "\" {}");
	CHECK_THROWS_AS(oms::readText(longName, section), oms::ParseError);
}

TEST_CASE("OmsText member order") {
	oms::Structure structure;
	structure.add("zebra", 1);
	structure.add("apple", 2);
	structure.add("mango", 3);
	oms::Structure& nested = structure.addStructure("banana");
	nested.add("z", 1);
	nested.add("a", 2);

	const std::string text = oms::toText(structure);
	CHECK(text.find("zebra") < text.find("apple"));
	CHECK(text.find("apple") < text.find("mango"));
	CHECK(text.find("mango") < text.find("banana"));

	const oms::Structure reread = oms::fromText(text);
	CHECK(reread.getEntries() == std::vector<std::string>{"zebra", "apple", "mango", "banana"});
	CHECK(static_cast<const oms::Structure&>(reread["banana"]).getEntries() == std::vector<std::string>{"z", "a"});

	// The order written by hand is the order read
	CHECK(oms::fromText("{ c: 1, a: 2, b: 3 }").getEntries() == std::vector<std::string>{"c", "a", "b"});
}

TEST_CASE("OmsText random structures") {
	for(int index = 0; index < 1000; ++index) {
		INFO("iteration " << index);
		oms::Structure structure;
		randomlyPopulateStructure(structure);

		const std::string text = oms::toText(structure);
		oms::Structure reread = oms::fromText(text);
		CHECK(reread == structure);

		// The same bytes: the same order, types and values, bit for bit
		std::stringstream original, roundTripped;
		original << structure;
		roundTripped << reread;
		CHECK((roundTripped.str() == original.str()));
	}
}

TEST_CASE("OmsText written by hand") {
	const oms::Structure settings = oms::fromText(R"(
		// Filter settings
		{
			gain: 0.5,
			taps: 3,
			label: "low pass",
			enabled: true,
			window: { start: -1, end: 1e3, },	// one line and a trailing comma are both fine
			"cutoff hz": 440.,
			bands: [ { low: 20, high: 200 }, { low: 200, high: 2e3 } ]
		}
	)");

	// Numbers written without a type are int32 and float8, and convert as any Variant does
	CHECK(settings["gain"].getType() == oms::DataType::float8);
	CHECK(settings["taps"].getType() == oms::DataType::int32);
	CHECK(settings.getOr("gain", 1.0f) == 0.5f);
	CHECK(settings.getOr("gain", 1.0) == 0.5);
	CHECK(settings.getOr("taps", 0) == 3);
	CHECK(settings.getOr("taps", 0.0f) == 3.0f);
	CHECK(settings.getOr("taps", 0.0) == 3.0);
	CHECK(settings.getOr("taps", std::uint8_t{0}) == 3);
	CHECK(settings.getOr("missing", 7) == 7);
	CHECK(settings.getOr("label", std::string()) == "low pass");
	CHECK(settings.getOr("enabled", false) == true);
	CHECK(settings.getOr("cutoff hz", 0.0f) == 440.0f);

	const oms::Structure& window = static_cast<const oms::Structure&>(settings["window"]);
	CHECK(window.getOr("start", 0.0f) == -1.0f);
	CHECK(window["start"].getType() == oms::DataType::int32);
	CHECK(window.getOr("end", 0) == 1000);
	CHECK(window["end"].getType() == oms::DataType::float8);

	const oms::Array& bands = static_cast<const oms::Array&>(settings["bands"]);
	REQUIRE(bands.size() == 2);
	CHECK(bands[1].getOr("high", 0.0f) == 2000.0f);

	// Every scalar may be given its type, the two default types included
	const oms::Structure typed = oms::fromText(
		"{a:int32(5),b:float8(0.5),c:float4(39),d:uint8 ( 7 ),e:float4[1, 2.5, -inf, nan,],f:uint8[],"
		" g:blob( 00 0102\n 03 ),h:float8(-nan),i:int64(-9223372036854775808),j:float4(inf)}");
	CHECK(typed["a"].getType() == oms::DataType::int32);
	CHECK(typed["a"] == 5);
	CHECK(typed["b"].getType() == oms::DataType::float8);
	CHECK(typed["b"] == 0.5);
	CHECK(typed["c"].getType() == oms::DataType::float4);
	CHECK(typed["c"] == 39.0f);
	CHECK(typed["d"].getType() == oms::DataType::uint8);
	CHECK(typed["d"] == 7);
	CHECK(typed["e"].getType() == oms::DataType::float4v);
	REQUIRE(typed["e"].size() == 4);
	const float* e = static_cast<const float*>(static_cast<void*>(typed["e"]));
	CHECK(e[1] == 2.5f);
	CHECK(e[2] == -std::numeric_limits<float>::infinity());
	CHECK(std::isnan(e[3]));
	CHECK(typed["f"].getType() == oms::DataType::uint8v);
	CHECK(typed["f"].size() == 0);
	CHECK(typed["g"].getType() == oms::DataType::blob);
	REQUIRE(typed["g"].size() == 4);
	CHECK(!memcmp(typed["g"], "\x00\x01\x02\x03", 4));
	CHECK(std::signbit(static_cast<double>(typed["h"])));
	CHECK(typed["i"] == std::numeric_limits<std::int64_t>::min());
	CHECK(typed["j"] == std::numeric_limits<float>::infinity());

	CHECK(oms::fromText("{}").empty());
	CHECK(oms::fromText("  {  }  // nothing\n").empty());
}

TEST_CASE("OmsText parse errors") {
	// The common mistakes, each on a line and column of its own
	checkParseError("{\n   a: uint12(5)\n}", 2, 7, "unknown type name 'uint12'");
	checkParseError("{\n   a: uint8(300)\n}", 2, 13, "300 does not fit uint8");
	checkParseError("{\n   a: 1,\n   b: 2,\n   a: 3\n}", 4, 4, "repeated key \"a\"");
	checkParseError("{\n   a: 1,\n   b: \"abc\n}", 3, 7, "unterminated string");
	checkParseError("{\n   a: 1\n   b: 2\n}", 3, 4, "expected ',' or '}', found 'b'");
	checkParseError("{\n   a: float4[4096 elided 9f3c2a1b]\n}", 2, 7, "elided");
	checkParseError("{\n   a: blob(131072 bytes elided 9f3c2a1b)\n}", 2, 7, "elided");

	// An elided value is recognized whatever its size and type would otherwise make of it
	checkParseError("{ a: uint8[4096 elided 9f3c2a1b] }", 1, 6, "elided");
	checkParseError("{ a: blob(100 bytes elided 9f3c2a1b) }", 1, 6, "elided");
	checkParseError("{ a: blob(65 bytes elided 00000000) }", 1, 6, "elided");
	checkParseError("{\n   a: [2008331 elided 9f3c2a1b]\n}", 2, 7, "elided");

	// A column is a byte, so a tab counts as one; a carriage return before a line feed changes nothing
	checkParseError("{\n\ta: uint12(5)\n}", 2, 5, "unknown type name");
	checkParseError("{\r\n   a: 1\r\n   b: 2\r\n}", 3, 4, "expected ',' or '}'");
	checkParseError("{ // a: uint12(5)\n   \"\xc3\xaf\": uint12(5) }", 2, 10, "unknown type name");

	// Numbers that do not fit
	checkParseError("{ a: 2147483648 }", 1, 6, "2147483648 does not fit int32");
	checkParseError("{ a: -2147483649 }", 1, 6, "does not fit int32");
	checkParseError("{ a: int8(128) }", 1, 11, "128 does not fit int8");
	checkParseError("{ a: int8(-129) }", 1, 11, "-129 does not fit int8");
	checkParseError("{ a: uint8(-1) }", 1, 12, "-1 does not fit uint8");
	checkParseError("{ a: uint16(65536) }", 1, 13, "does not fit uint16");
	checkParseError("{ a: int16(32768) }", 1, 12, "does not fit int16");
	checkParseError("{ a: uint32(4294967296) }", 1, 13, "does not fit uint32");
	checkParseError("{ a: int64(9223372036854775808) }", 1, 12, "does not fit int64");
	checkParseError("{ a: uint64(18446744073709551616) }", 1, 13, "does not fit uint64");
	checkParseError("{ a: 1e999 }", 1, 6, "1e999 does not fit float8");
	checkParseError("{ a: float4(1e39) }", 1, 13, "1e39 does not fit float4");
	checkParseError("{ a: float8(1e-999) }", 1, 13, "does not fit float8");
	checkParseError("{ a: int16[1, 2, 40000] }", 1, 18, "40000 does not fit int16");

	// Numbers that are not numbers
	checkParseError("{ a: int32(1.5) }", 1, 12, "expected an integer, found '1.5'");
	checkParseError("{ a: uint8[1, 2.0] }", 1, 15, "expected an integer");
	checkParseError("{ a: int32(nan) }", 1, 12, "expected an integer");
	checkParseError("{ a: 1.5.2 }", 1, 6, "expected a number, found '1.5.2'");
	checkParseError("{ a: 12abc }", 1, 6, "expected a number");
	checkParseError("{ a: 0x10 }", 1, 6, "expected a number");
	checkParseError("{ a: 1e }", 1, 6, "expected a number");
	checkParseError("{ a: - }", 1, 6, "expected a number");
	checkParseError("{ a: +5 }", 1, 6, "expected a number");
	checkParseError("{ a: float4(infinity) }", 1, 13, "expected a number");
	checkParseError("{ a: float4(NaN) }", 1, 13, "expected a number");
	checkParseError("{ a: float4() }", 1, 13, "expected a number, found ')'");

	// Values
	checkParseError("{ a: }", 1, 6, "expected a value, found '}'");
	checkParseError("{ a: maybe }", 1, 6, "unknown type name 'maybe'");
	checkParseError("{ a: True }", 1, 6, "unknown type name 'True'");
	checkParseError("{ a: int32 5 }", 1, 12, "expected '(' or '['");
	checkParseError("{ a: int32(5 }", 1, 14, "expected ')'");
	checkParseError("{ a: [1, 2, 3] }", 1, 7, "a vector is written with its element type");
	checkParseError("{ a: [{}, 5] }", 1, 11, "expected '{'");
	checkParseError("{ a: [{} {}] }", 1, 10, "expected ',' or ']'");
	checkParseError("{ a: int16[1 2] }", 1, 14, "expected ',' or ']', found '2'");
	checkParseError("{ a: int16[1 x] }", 1, 14, "expected ',' or ']', found 'x'");
	checkParseError("{ a: blob[00] }", 1, 10, "expected '(' after blob");
	checkParseError("{ a: blob(0) }", 1, 11, "two hexadecimal digits per byte");
	checkParseError("{ a: blob(00 0g) }", 1, 14, "two hexadecimal digits per byte");
	checkParseError("{ a: blob(00, 01) }", 1, 13, "expected hexadecimal digits or ')', found ','");

	// Strings
	checkParseError("{ a: \"\\q\" }", 1, 7, "unknown escape");
	checkParseError("{ a: \"\\x4\" }", 1, 7, "two hexadecimal digits after \\x");
	checkParseError("{ a: \"abc", 1, 6, "unterminated string");
	checkParseError("{ \"abc: 1 }\n", 1, 3, "unterminated string");

	// Structure
	checkParseError("", 1, 1, "expected '{', found the end of the text");
	checkParseError("\n\n   ", 3, 4, "expected '{', found the end of the text");
	checkParseError("section \"a\" { x: 1 }", 1, 1, "expected '{', found 's'");
	checkParseError("[]", 1, 1, "expected '{'");
	checkParseError("{ a: 1 } x", 1, 10, "expected the end of the text, found 'x'");
	checkParseError("{ a: 1 } {}", 1, 10, "expected the end of the text");
	checkParseError("{ a: 1", 1, 7, "expected ',' or '}', found the end of the text");
	checkParseError("{ a: 1,", 1, 8, "expected a key or '}', found the end of the text");
	checkParseError("{ a 1 }", 1, 5, "expected ':' after the key, found '1'");
	checkParseError("{ 5: 1 }", 1, 3, "expected a key or '}', found '5'");
	checkParseError("{ a: 1,, }", 1, 8, "expected a key or '}', found ','");
	checkParseError("{ a: {b: 1 }", 1, 13, "expected ',' or '}'");
	checkParseError("{ a: 1 / b: 2 }", 1, 8, "expected '//' to start a comment");
	checkParseError("{ a: 1, \x01 }", 1, 9, "found byte 0x01");
}

TEST_CASE("OmsText summary") {
	std::vector<float> samples(4096);
	for(size_t index = 0; index < samples.size(); ++index)
		samples[index] = static_cast<float>(index) / 8;
	std::vector<std::uint8_t> image(131072, 0x5a);

	oms::Structure structure;
	structure.add("name", "experiment");
	structure.addVector("samples", samples);
	structure.addVector<std::int16_t>("point", {180, 94});
	structure.add("image", image.data(), image.size());
	structure.add("tag", image.data(), 4);
	oms::Structure& nested = structure.addArray("items").addStructure();
	nested.addVector<double>("seventeen", std::vector<double>(17, 1.5));
	nested.addVector<double>("sixteen", std::vector<double>(16, 1.5));
	nested.add("sixtyFive", image.data(), 65);
	nested.add("sixtyFour", image.data(), 64);

	// A vector or blob is elided only when it is over the limit, not when it is at it
	const oms::TextOptions summary = {.maxVectorElements = 16, .maxBlobBytes = 64};
	const std::string text = oms::toText(structure, summary);
	CHECK(text ==
		"{\n"
		"   name: \"experiment\",\n"
		"   samples: float4[4096 elided 611b539b],\n"
		"   point: int16[180, 94],\n"
		"   image: blob(131072 bytes elided 090c9dc5),\n"
		"   tag: blob(5a5a5a5a),\n"
		"   items: [\n"
		"      {\n"
		"         seventeen: float8[17 elided 55ee8cc0],\n"
		"         sixteen: float8[\n"
		"            1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5,\n"
		"            1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5\n"
		"         ],\n"
		"         sixtyFive: blob(65 bytes elided 9520fb4d),\n"
		"         sixtyFour: blob(\n"
		"            5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a\n"
		"            5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a 5a5a5a5a\n"
		"         )\n"
		"      }\n"
		"   ]\n"
		"}");

	// The same data gives the same text every time, from a copy as much as from the original
	CHECK(oms::toText(structure, summary) == text);
	CHECK(oms::toText(oms::Structure(structure), summary) == text);

	// A change to elided contents changes the text, although the contents are not shown
	samples[2000] += 0.125f;
	structure.addVector("samples", samples);
	const std::string changedVector = oms::toText(structure, summary);
	CHECK(changedVector != text);
	CHECK(changedVector.find("samples: float4[4096 elided ") != std::string::npos);

	image[70000] ^= 1;
	structure.add("image", image.data(), image.size());
	const std::string changedBlob = oms::toText(structure, summary);
	CHECK(changedBlob != changedVector);
	CHECK(changedBlob.find("image: blob(131072 bytes elided ") != std::string::npos);

	// A summary is for reading and comparing; it does not convert back
	checkParseError(text, 3, 13, "elided");
	const std::string blobsOnly = oms::toText(structure, {.maxBlobBytes = 64});
	checkParseError(blobsOnly, 518, 11, "elided");

	// Sections take the same options
	oms::Section section;
	section.name = "data";
	section.addVector("samples", samples);
	std::stringstream stream;
	oms::writeText(stream, section, {.maxVectorElements = 0});
	CHECK(stream.str().starts_with("section \"data\" {\n   samples: float4[4096 elided "));
	CHECK_THROWS_AS(oms::readText(stream, section), oms::ParseError);

	// With no limits nothing is elided, however large
	CHECK(oms::toText(structure).find("elided") == std::string::npos);
	checkRoundTrip(structure);
}

TEST_CASE("OmsText summary of arrays") {
	oms::Structure structure;
	oms::Array& rows = structure.addArray("rows");
	for(int index = 0; index < 3; ++index)
		rows.addStructure().add("i", index);
	oms::Array& pair = structure.addArray("pair");
	pair.addStructure().add("i", 0);
	pair.addStructure().add("i", 1);
	structure.addArray("none");

	// An array is elided only when it is over the limit. Its hash is that of its own full text,
	// here "[\n   {\n      i: 0\n   },\n   {\n      i: 1\n   },\n   {\n      i: 2\n   }\n]".
	const std::string text = oms::toText(structure, {.maxArrayElements = 2});
	CHECK(text ==
		"{\n"
		"   rows: [3 elided 08c7c1e5],\n"
		"   pair: [\n"
		"      {\n"
		"         i: 0\n"
		"      },\n"
		"      {\n"
		"         i: 1\n"
		"      }\n"
		"   ],\n"
		"   none: []\n"
		"}");
	CHECK(oms::toText(structure, {.maxArrayElements = 2}) == text);

	// An empty array is never over a limit
	CHECK(oms::toText(structure, {.maxArrayElements = 0}) ==
		"{\n   rows: [3 elided 08c7c1e5],\n   pair: [2 elided df15e4f2],\n   none: []\n}");

	// The hash is the same wherever the array sits
	oms::Structure nested;
	oms::Array& deepRows = nested.addStructure("a").addArray("b").addStructure().addArray("rows");
	for(int index = 0; index < 3; ++index)
		deepRows.addStructure().add("i", index);
	CHECK(oms::toText(nested, {.maxArrayElements = 2}).find("rows: [3 elided 08c7c1e5]") != std::string::npos);

	// A summary does not convert back
	checkParseError(text, 2, 10, "elided");
	checkParseError("{ a: [3 elided 08c7c1e5] }", 1, 6, "elided");
	checkParseError("{ a: [ 3\n elided 08c7c1e5 ] }", 1, 6, "elided");
	// ...but numbers in a bare '[' are otherwise still taken for a vector missing its type
	checkParseError("{ a: [3 elide 08c7c1e5] }", 1, 7, "a vector is written with its element type");
	checkParseError("{ a: [3] }", 1, 7, "a vector is written with its element type");

	// The hash covers everything the array holds, in full, whatever the other limits are
	std::vector<float> samples(100);
	for(size_t index = 0; index < samples.size(); ++index)
		samples[index] = static_cast<float>(index) / 4;
	std::vector<std::uint8_t> bytes(100, 0x5a);

	auto build = [&](oms::Structure& holder) {
		oms::Array& records = holder.addArray("records");
		for(int index = 0; index < 5; ++index) {
			oms::Structure& record = records.addStructure();
			record.add("id", index);
			record.addVector("samples", samples);
			record.add("bytes", bytes.data(), bytes.size());
			record.addArray("children").addStructure().add("name", std::format("child {}", index));
		}
	};
	oms::Structure records;
	build(records);

	const oms::TextOptions arraysOnly = {.maxArrayElements = 4};
	const oms::TextOptions everything = {.maxVectorElements = 0, .maxBlobBytes = 0, .maxArrayElements = 4};
	const std::string summary = oms::toText(records, arraysOnly);
	CHECK(summary == std::format("{{\n   records: [5 elided {:08x}]\n}}", fnv1a(loneArrayText(records))));
	CHECK(oms::toText(records, everything) == summary);

	// So a change anywhere inside shows: in a vector, a blob, a nested array, a key or a type
	samples[50] += 0.25f;
	oms::Structure changedVector;
	build(changedVector);
	CHECK(oms::toText(changedVector, everything) != summary);
	samples[50] -= 0.25f;

	bytes[99] ^= 1;
	oms::Structure changedBlob;
	build(changedBlob);
	CHECK(oms::toText(changedBlob, everything) != summary);
	bytes[99] ^= 1;

	oms::Structure unchanged;
	build(unchanged);
	CHECK(oms::toText(unchanged, everything) == summary);
	oms::Array& unchangedRecords = unchanged.getOrAddArray("records");

	unchangedRecords[4].addArray("children").addStructure().add("name", "child four");
	CHECK(oms::toText(unchanged, everything) != summary);
	unchangedRecords[4].addArray("children").addStructure().add("name", "child 4");
	CHECK(oms::toText(unchanged, everything) == summary);

	unchangedRecords[2].add("id", std::int64_t{2});		// the same value under another type
	CHECK(oms::toText(unchanged, everything) != summary);
	unchangedRecords[2].add("id", 2);
	CHECK(oms::toText(unchanged, everything) == summary);

	// An array whose text is too long for the writer to hold at once is hashed in pieces, to the same result
	oms::Structure large;
	oms::Array& many = large.addArray("many");
	for(int index = 0; index < 5000; ++index) {
		oms::Structure& element = many.addStructure();
		element.add("id", std::uint32_t(index * 2654435761u));
		element.add("baselineFrequency", static_cast<float>(index) / 5000);
	}
	const std::string manyText = loneArrayText(large);
	CHECK(manyText.size() > 300000);
	CHECK(oms::toText(large, {.maxArrayElements = 256}) ==
		std::format("{{\n   many: [5000 elided {:08x}]\n}}", fnv1a(manyText)));

	// Sections take the limit too, and writing one to a stream gives the same hash
	oms::Section section;
	section.name = "data";
	oms::Array& sectionMany = section.addArray("many");
	for(int index = 0; index < 5000; ++index) {
		oms::Structure& element = sectionMany.addStructure();
		element.add("id", std::uint32_t(index * 2654435761u));
		element.add("baselineFrequency", static_cast<float>(index) / 5000);
	}
	std::stringstream stream;
	oms::writeText(stream, section, {.maxArrayElements = 256});
	CHECK(stream.str() == std::format("section \"data\" {{\n   many: [5000 elided {:08x}]\n}}\n", fnv1a(manyText)));
	CHECK_THROWS_AS(oms::readText(stream, section), oms::ParseError);

	// With no limit nothing is elided
	CHECK(oms::toText(large).find("elided") == std::string::npos);
	checkRoundTrip(records);
}

TEST_CASE("OmsText reserved section fields") {
	oms::Section section;
	section.name = "config";
	section.add("x", 1);
	std::stringstream stream;
	stream << section;
	const std::string bytes = stream.str();

	// A section header is two 8-byte GUIDs, the section's size, and then two reserved fields
	// that are zero in every file written so far. Text has no place for them, so rather than
	// drop a value that some later writer put there, writeText refuses.
	const size_t firstReserved = 16 + sizeof(size_t);
	const size_t secondReserved = firstReserved + sizeof(size_t);
	std::stringstream text;

	for(const size_t offset : {firstReserved, secondReserved}) {
		std::string patched = bytes;
		patched[offset] = 1;

		std::stringstream read(patched);
		oms::Section reread;
		read >> reread;
		CHECK(reread.name == "config");
		CHECK_THROWS_AS(oms::writeText(text, reread), std::invalid_argument);

		std::stringstream search(patched);
		auto found = oms::Section::findNext(search, "config");
		REQUIRE(found.has_value());
		CHECK_THROWS_AS(oms::writeText(text, *found), std::invalid_argument);
	}
	CHECK(text.str().empty());

	std::stringstream untouched(bytes);
	auto found = oms::Section::findNext(untouched, "config");
	REQUIRE(found.has_value());
	CHECK_NOTHROW(oms::writeText(text, *found));
	CHECK(text.str() == "section \"config\" {\n   x: 1\n}\n");
}

TEST_CASE("OmsText unsupported types") {
	// A Vector of a type with no DataType can be held in memory, but it has no text form
	// (nor a binary one)
	oms::Structure structure;
	structure.addVector<bool>("flags", {true, false});
	CHECK_THROWS_AS(oms::toText(structure), std::invalid_argument);
}
