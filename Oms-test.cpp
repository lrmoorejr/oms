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

#include <random>
#include <sstream>
#include <format>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Oms.hpp"
#include "OmsString.hpp"

void randomlyPopulateStructure(oms::Structure& structure, int oddsOfArray = 1);

TEST_CASE("Basic Tests") {
	oms::Structure structure;

	structure.add("four", 4);
	int i = structure["four"];
	CHECK(i == 4);

	structure.add("greeting", "hello");
	std::string s = structure["greeting"];
	CHECK(s == "hello");
}

TEST_CASE("Aggregate structure") {
	oms::Structure root;
	oms::Structure& s = root.addStructure("aggregate");
	s.add("name", 3);
	CHECK(s.get("name") == 3);
	CHECK(root["aggregate"]["name"] == 3);
}

TEST_CASE("Vector") {
	oms::Structure root;
	oms::Structure* s = &root;

	oms::Vector<int>& v = s->addVector<int>("v", {1, 2, 3});
	for(int i = 0; i < 3; ++i) {
		CHECK(v[i] == i + 1);
	}

	// std::cout << toString(root) << std::endl;
}

TEST_CASE("Array") {
	oms::Structure root;
	oms::Structure* s = &root;

	oms::Array& array = s->addArray("array");
	for(int i = 0; i < 3; ++i) {
		oms::Structure& element = array.addStructure();
		element.add("x", i);
	}
	for(int i = 0; i < 3; ++i) {
		int x = reinterpret_cast<const oms::Array&>((*s)["array"])[i]["x"];
		CHECK(x == i);
	}
}

TEST_CASE("Simple Serialize / Deserialize") {
	oms::Structure structure;
	structure.add("four", 4);
	structure.add("str", "a string");
	structure.add("bool", true);

	int i = structure["four"];
	CHECK(i == 4);
	std::string string = structure["str"];
	CHECK(string == "a string");

	std::stringstream stream;
	stream << structure;
	
	oms::Structure structure2;
	stream >> structure2;

	i = structure2["four"];
	CHECK(i == 4);
	std::string string2 = structure2["str"];
	CHECK(string2 == "a string");
	CHECK(structure2["bool"] == true);
}

std::string randomString(size_t maxLength = 1000) {
	static std::mt19937 generator(0);

	std::stringstream buffer;
	std::uniform_int_distribution<> sizeDistribution(1, maxLength);
	size_t size = sizeDistribution(generator);

	std::uniform_int_distribution<int> charDistribution(32, 127);

	for(size_t index = 0; index < size; ++index)
		buffer.put(charDistribution(generator));
	return buffer.str();
}

template <class T>
std::vector<T> randomVector(size_t maxLength = 100) {
	static std::mt19937 generator(0);

	std::uniform_int_distribution<> sizeDistribution(1, maxLength);
	const size_t size = sizeDistribution(generator);
	std::vector<T> data(size);

	for(size_t index = 0; index < size; ++index)
		data.push_back(generator() * 100);

	return data;
}

std::vector<std::uint8_t> randomBlob(size_t maxLength = 100) {
	static std::mt19937 generator(0);

	std::uniform_int_distribution<> sizeDistribution(1, maxLength);
	size_t size = sizeDistribution(generator);
	std::vector<std::uint8_t> buffer(size);

	std::uniform_int_distribution<std::uint8_t> byteDistribution;

	for(size_t index = 0; index < size; ++index)
		buffer.push_back(byteDistribution(generator));
	return buffer;
}

void randomlyPopulateArray(oms::Array& array, size_t maxLength = 100) {
	static std::mt19937 generator(0);

	std::uniform_int_distribution<> sizeDistribution(1, maxLength);
	size_t size = sizeDistribution(generator);

	for(size_t index = 0; index < size; ++index) {
		oms::Structure& element = array.addStructure();
		randomlyPopulateStructure(element, size);
	}
}

void randomlyPopulateStructure(oms::Structure& structure, int oddsOfArray) {
	static std::mt19937 generator(0);
	static size_t identifierIndex = 0;

	std::uniform_int_distribution<> memberCountDistribution(1, static_cast<int>(oms::DataType::_last) - 1);
	const int memberCount = memberCountDistribution(generator);

	std::uniform_int_distribution<> typeDistribution(1, static_cast<int>(oms::DataType::_last) - 1);

	std::vector<std::uint8_t> blobHolder;

	for(auto memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
		const oms::DataType type = static_cast<oms::DataType>(typeDistribution(generator));
		switch(type) {
		case oms::DataType::_last:
		case oms::DataType::unsupported:
			CHECK(false);
			break;
		case oms::DataType::structure:
			if(std::uniform_int_distribution<std::uint32_t>(0, oddsOfArray)(generator) == 0) {
				oms::Structure& child = structure.addStructure(std::format("{}", identifierIndex++));
				randomlyPopulateStructure(child);
			}
			break;
		case oms::DataType::array:
			if(std::uniform_int_distribution<std::uint32_t>(0, oddsOfArray)(generator) == 0) {
				oms::Array& child = structure.addArray(std::format("{}", identifierIndex++));
				randomlyPopulateArray(child);
			}
			break;
		case oms::DataType::string:
			structure.add(std::format("{}", identifierIndex++), randomString());
			break;
		case oms::DataType::uint8:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::uint8_t>()(generator));
			break;
		case oms::DataType::uint16:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::uint16_t>()(generator));
			break;
		case oms::DataType::uint32:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::uint32_t>()(generator));
			break;
		case oms::DataType::uint64:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::uint64_t>()(generator));
			break;
		case oms::DataType::int8:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::int8_t>()(generator));
			break;
		case oms::DataType::int16:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::int16_t>()(generator));
			break;
		case oms::DataType::int32:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::int32_t>()(generator));
			break;
		case oms::DataType::int64:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<std::int64_t>()(generator));
			break;
		case oms::DataType::float4:
			structure.add(std::format("{}", identifierIndex++), std::uniform_real_distribution<float>()(generator));
			break;
		case oms::DataType::float8:
			structure.add(std::format("{}", identifierIndex++), std::uniform_real_distribution<double>()(generator));
			break;
		case oms::DataType::boolean:
			structure.add(std::format("{}", identifierIndex++), std::uniform_int_distribution<int>(0, 1)(generator) == 0);

		case oms::DataType::uint8v:
			structure.addVector<std::uint8_t>(std::format("{}", identifierIndex++), randomVector<std::uint8_t>());
			break;
		case oms::DataType::uint16v:
			structure.addVector<std::uint16_t>(std::format("{}", identifierIndex++), randomVector<std::uint16_t>());
			break;
		case oms::DataType::uint32v:
			structure.addVector<std::uint32_t>(std::format("{}", identifierIndex++), randomVector<std::uint32_t>());
			break;
		case oms::DataType::uint64v:
			structure.addVector<std::uint64_t>(std::format("{}", identifierIndex++), randomVector<std::uint64_t>());
			break;
		case oms::DataType::int8v:
			structure.addVector<std::int8_t>(std::format("{}", identifierIndex++), randomVector<std::int8_t>());
			break;
		case oms::DataType::int16v:
			structure.addVector<std::int16_t>(std::format("{}", identifierIndex++), randomVector<std::int16_t>());
			break;
		case oms::DataType::int32v:
			structure.addVector<std::int32_t>(std::format("{}", identifierIndex++), randomVector<std::int32_t>());
			break;
		case oms::DataType::int64v:
			structure.addVector<std::int64_t>(std::format("{}", identifierIndex++), randomVector<std::int64_t>());
			break;
		case oms::DataType::float4v:
			structure.addVector<std::float_t>(std::format("{}", identifierIndex++), randomVector<std::float_t>());
			break;
		case oms::DataType::float8v:
			structure.addVector<std::double_t>(std::format("{}", identifierIndex++), randomVector<std::double_t>());
			break;
		case oms::DataType::blob:
			blobHolder = randomBlob();
			structure.add(std::format("{}", identifierIndex++), blobHolder.data(), blobHolder.size());
			break;
		}
	}
}

TEST_CASE("Random Structures") {
	for(int index = 0; index < 1000; ++index) {
		oms::Structure structure;
		randomlyPopulateStructure(structure);
	
		std::stringstream stream;
		stream << structure;
		
		oms::Structure structure2;
		stream >> structure2;
	
		std::string structureString = oms::toString(structure);
		std::string structure2String = oms::toString(structure2);
		CHECK(structureString == structure2String);
	}
}

TEST_CASE("Design Test") {
	oms::Section section;
	std::stringstream stream;

	// Build up a document Section with various kinds of data
	section.name = "section 1";	// Optional

	section.add("a char", 'a');
	section.add("asdf-bool", true);
	section.add("fff", 0.3f);
	section.add("ddd", 1.1);

	oms::Structure& structure = section.addStructure("struct");
	structure.add("item", "string");

	auto& array = section.addArray("ss");
	for(int index = 0; index < 100; ++index) {
		oms::Structure& element = array.addStructure();
		element.add("i", index);
		element.add("i2", 100 - index);
	}

	std::vector<int> stuff(10);
	section.add("blob", stuff.data(), stuff.size() * sizeof(int));
	
	// Serialize the Section
	stream << section;

	// Build up another section
	section.name = "section 2";	// Optional

	section.add("something else", 10LL);

	// Serialize the second Section
	stream << section;

	// Confirm that the data is no long in memory
	CHECK(section.empty());

	
	// Unserialize the first Section we just saved
	oms::Section section1Check;
	stream >> section1Check;

	// Confirm that data is intact
	CHECK(section1Check.name == "section 1");
	CHECK(section1Check["a char"] == 'a');
	CHECK(section1Check["asdf-bool"] == true);
	CHECK(section1Check["fff"] == 0.3f);
	CHECK(section1Check["ddd"] == 1.1);
	CHECK(section1Check["struct"]["item"] == std::string("string"));
	for(int index = 0; index < 100; ++index) {
		CHECK(reinterpret_cast<const oms::Array&>(section1Check["ss"])[index]["i"] == index);
		CHECK(reinterpret_cast<const oms::Array&>(section1Check["ss"])[index]["i2"] == (100 - index));
	}
	CHECK(section1Check["blob"].size() == stuff.size() * sizeof(int));
	CHECK(!memcmp(stuff.data(), section1Check["blob"], section1Check["blob"].size()));

	// Unserialize the second Section
	oms::Section section2Check;
	stream >> section2Check;

	CHECK(section2Check.name == "section 2");
	CHECK(section2Check["something else"] == 10LL);
	CHECK(section2Check.size() == 1);
}

TEST_CASE("getOr() read-only default") {
	oms::Structure structure;
	structure.add("x", 10);
	structure.add("label", std::string("hello"));
	structure.add("flag", true);

	// Returns the stored value when the key exists.
	CHECK(structure.getOr("x", 99) == 10);
	CHECK(structure.getOr("label", std::string("fallback")) == std::string("hello"));
	CHECK(structure.getOr("flag", false) == true);

	// Returns the default when the key is absent -- and does NOT insert it.
	CHECK(structure.getOr("missing", 42) == 42);
	CHECK(structure.getOr("missing_str", std::string("default")) == std::string("default"));
	CHECK(structure.getOr("missing_dbl", 3.14) == 3.14);
	CHECK(!structure.contains("missing"));
	CHECK(!structure.contains("missing_str"));
	CHECK(!structure.contains("missing_dbl"));
	CHECK(structure.size() == 3);  // structure is unchanged

	// Callable on a const reference -- getOrAdd is not.
	const oms::Structure& ref = structure;
	CHECK(ref.getOr("x", 0) == 10);
	CHECK(ref.getOr("absent", 7) == 7);
	CHECK(ref.size() == 3);

	// Contrast: getOrAdd inserts.
	structure.getOrAdd("new_key", 7);
	CHECK(structure.contains("new_key"));
	CHECK(structure.size() == 4);
}

TEST_CASE("getOrAdd() functions") {
	oms::Section section;
	CHECK(section.empty());

	// Will add a: 3
	CHECK(section.getOrAdd("a", 3) == 3);
	// a is already set to 3, so this won't work
	CHECK(section.getOrAdd("a", std::string("default")) != std::string("default"));
	// A fresh identifier should work fine, though
	CHECK(section.getOrAdd("zzz", std::string("default")) == std::string("default"));
	// Try with a blob
	char defaultData[10];
	CHECK(section.getOrAdd("b", defaultData, 10).size() == 10);

	// Try with a structure
	oms::Structure& defaultStructure = section.getOrAddStructure("z");
	CHECK(defaultStructure.empty());

	// We should be back at our root structure, and "a" should already be defined as 3
	CHECK(section.getOrAdd("a", 4) == 3);

	const oms::Array& defaultArray = section.getOrAddArray("t");
	CHECK(defaultArray.empty());

	CHECK(section.getOrAddStructure("m").getOrAdd("z", 3.2) == 3.2);

	CHECK(section.getOrAddStructure("m").getOrAddStructure("n").getOrAdd("z", 3.2) == 3.2);

#if 0
	section.clear();
	CHECK(section.getOrAddStructure("m").getOrAddArray("z").getOrAdd(3, 4.1) == 4.1);
	CHECK(oms::getOrAddStructure("m").getOrAddArray("z").getOrAdd(3, "one") == std::string("one"));
	// Cannot getOr...() into arrays
	// CHECK(oms::getOrAddArray("a").getOrAddStructure(3).getOrAddArray("x").getOrAdd(4, false) == false);
	// CHECK(oms::getOrAddArray("a").getOrAddArray(3).getOrAddArray(5).getOrAdd(4, false) == false);
#endif
}

TEST_CASE("OmsString formatting") {
	// Scalar types
	{
		oms::Structure s;
		s.add("flag", true);
		s.add("zero", false);
		std::string out = oms::toString(s);
		CHECK(out.find("true") != std::string::npos);
		CHECK(out.find("false") != std::string::npos);
		CHECK(out.find(": 1") == std::string::npos);  // must not emit 0/1 for bools
		CHECK(out.find(": 0") == std::string::npos);
	}
	{
		oms::Structure s;
		s.add("i", 42);
		s.add("f", 3.14f);
		s.add("d", 2.718);
		s.add("str", std::string("hello"));
		std::string out = oms::toString(s);
		CHECK(out.find("42") != std::string::npos);
		CHECK(out.find("3.14") != std::string::npos);
		CHECK(out.find("2.718") != std::string::npos);
		CHECK(out.find("\"hello\"") != std::string::npos);  // strings are quoted
	}

	// Blobs show as "(blob)"
	{
		oms::Structure s;
		std::uint8_t data[] = {1, 2, 3};
		s.add("b", data, sizeof(data));
		std::string out = oms::toString(s);
		CHECK(out.find("(blob)") != std::string::npos);
	}

	// Integer vectors
	{
		oms::Structure s;
		s.addVector<std::int32_t>("v", {10, 20, 30});
		std::string out = oms::toString(s);
		CHECK(out.find("[") != std::string::npos);
		CHECK(out.find("10") != std::string::npos);
		CHECK(out.find("20") != std::string::npos);
		CHECK(out.find("30") != std::string::npos);
	}

	// Float vectors (exercises the float/double path fixed from float_t/double_t)
	{
		oms::Structure s;
		s.addVector<float>("fv", {1.0f, 2.0f});
		s.addVector<double>("dv", {3.0, 4.0});
		std::string out = oms::toString(s);
		CHECK(out.find("1") != std::string::npos);
		CHECK(out.find("3") != std::string::npos);
	}

	// Nested structure
	{
		oms::Structure root;
		oms::Structure& child = root.addStructure("inner");
		child.add("x", 7);
		std::string out = oms::toString(root);
		CHECK(out.find("inner") != std::string::npos);
		CHECK(out.find("7") != std::string::npos);
	}

	// Nested array
	{
		oms::Structure root;
		oms::Array& arr = root.addArray("items");
		arr.addStructure().add("n", 1);
		arr.addStructure().add("n", 2);
		std::string out = oms::toString(root);
		CHECK(out.find("items") != std::string::npos);
		CHECK(out.find("1") != std::string::npos);
		CHECK(out.find("2") != std::string::npos);
	}

	// Keys are emitted in sorted (alphabetical) order regardless of insertion order
	{
		oms::Structure s;
		s.add("zebra", 1);
		s.add("apple", 2);
		s.add("mango", 3);
		std::string out = oms::toString(s);
		CHECK(out.find("apple") < out.find("mango"));
		CHECK(out.find("mango") < out.find("zebra"));
	}
}

TEST_CASE("Little-endian wire format") {
	// Serialize a structure containing known integer and float values, then inspect
	// the raw bytes to confirm they are stored in little-endian order regardless of
	// host endianness.  On a LE host this also guards against accidental double-swap.
	oms::Structure structure;
	structure.add("u16", static_cast<std::uint16_t>(0x1234));
	structure.add("u32", static_cast<std::uint32_t>(0x12345678));
	structure.add("u64", static_cast<std::uint64_t>(0x123456789ABCDEF0ULL));

	std::stringstream stream;
	stream << structure;
	std::string bytes = stream.str();

	// Locate each value by scanning for the expected LE bytes.
	// (The surrounding framing — member count, identifier lengths, DataType tags —
	//  are all single-byte or already-tested fields; we only verify the payloads.)
	auto containsLE = [&](std::initializer_list<unsigned char> expected) {
		const std::string pat(expected.begin(), expected.end());
		return bytes.find(pat) != std::string::npos;
	};

	// 0x1234 in LE: 34 12
	CHECK(containsLE({0x34, 0x12}));
	// 0x12345678 in LE: 78 56 34 12
	CHECK(containsLE({0x78, 0x56, 0x34, 0x12}));
	// 0x123456789ABCDEF0 in LE: F0 DE BC 9A 78 56 34 12
	CHECK(containsLE({0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12}));

	// Round-trip: confirm values survive the encode/decode cycle.
	oms::Structure structure2;
	stream >> structure2;
	CHECK(static_cast<std::uint16_t>(structure2["u16"]) == 0x1234);
	CHECK(static_cast<std::uint32_t>(structure2["u32"]) == 0x12345678);
	CHECK(static_cast<std::uint64_t>(structure2["u64"]) == 0x123456789ABCDEF0ULL);
}

TEST_CASE("Section navigation") {
	std::stringstream stream;

	// Write two sections and capture their start positions.
	auto pos0 = stream.tellp();
	oms::Section s1;
	s1.name = "first";
	s1.add("x", 42);
	s1.add("label", std::string("alpha"));
	stream << s1;
	auto pos1 = stream.tellp();	// start of second section

	oms::Section s2;
	s2.name = "second";
	s2.add("y", 99);
	stream << s2;
	auto pos2 = stream.tellp();	// end of second section

	// Read back both sections normally.
	stream.seekg(0);
	oms::Section r1, r2;
	stream >> r1;
	stream >> r2;

	// sectionSize() must equal the actual bytes consumed by each section.
	CHECK(r1.sectionSize() == static_cast<size_t>(pos1 - pos0));
	CHECK(r2.sectionSize() == static_cast<size_t>(pos2 - pos1));
	CHECK(r1.sectionSize() > 0);
	CHECK(r2.sectionSize() > 0);

	// The seek-forward idiom: start at the beginning, skip s1 by size, read s2 directly.
	stream.seekg(0);
	auto sectionStart = stream.tellg();
	oms::Section peek;
	stream >> peek;		// load s1 to get its sectionSize()
	CHECK(peek.name == "first");
	stream.seekg(static_cast<std::streamoff>(sectionStart) + static_cast<std::streamoff>(peek.sectionSize()));
	oms::Section r2b;
	stream >> r2b;
	CHECK(r2b.name == "second");
	CHECK(static_cast<int>(r2b["y"]) == 99);
}

TEST_CASE("Section::find") {
	std::stringstream stream;

	// Write three sections.
	oms::Section s1;
	s1.name = "alpha";
	s1.add("v", 1);
	stream << s1;

	oms::Section s2;
	s2.name = "beta";
	s2.add("v", 2);
	s2.add("extra", std::string("data"));
	stream << s2;

	oms::Section s3;
	s3.name = "gamma";
	s3.add("v", 3);
	stream << s3;

	// Find the middle section -- exercises the skip path for s1 and the match path for s2.
	stream.seekg(0);
	auto result = oms::Section::findNext(stream,"beta");
	REQUIRE(result.has_value());
	CHECK(result->name == "beta");
	CHECK(static_cast<int>((*result)["v"]) == 2);
	CHECK(static_cast<std::string>((*result)["extra"]) == "data");

	// Find the last section -- exercises two skips.
	stream.seekg(0);
	auto result2 = oms::Section::findNext(stream,"gamma");
	REQUIRE(result2.has_value());
	CHECK(result2->name == "gamma");
	CHECK(static_cast<int>((*result2)["v"]) == 3);

	// Find the first section -- no skip needed.
	stream.seekg(0);
	auto result3 = oms::Section::findNext(stream,"alpha");
	REQUIRE(result3.has_value());
	CHECK(result3->name == "alpha");
	CHECK(static_cast<int>((*result3)["v"]) == 1);

	// Search for a name that doesn't exist -- should return nullopt.
	stream.seekg(0);
	auto result4 = oms::Section::findNext(stream,"missing");
	CHECK(!result4.has_value());
}
