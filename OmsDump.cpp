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

#include <fstream>
#include <iostream>
#include "OmsString.hpp"
#include "OmsText.hpp"

// Summary mode elides vectors longer than this many elements, blobs larger than the same
// number of 4-byte values, and arrays longer than 16 times as many structures. Arrays get
// the higher limit because the short ones are what gives a file its shape.
const size_t defaultSummaryLimit = 16;

// limit * factor, held at the maximum rather than wrapping around
size_t scaledLimit(size_t limit, size_t factor) {
	const size_t max = std::numeric_limits<size_t>::max();
	return limit > max / factor ? max : limit * factor;
}

// Text to binary. The binary is put together in memory and only written out once all of
// the text has parsed, so that a parse error leaves no output file behind.
int convertToBinary(const std::string& textPath, const std::string& binaryPath) {
	const bool fromStdin = textPath == "-";
	std::ifstream textFile;
	if(!fromStdin) {
		textFile.open(textPath);
		if(!textFile) {
			std::cerr << "Cannot open " << textPath << '\n';
			return 1;
		}
	}
	std::istream& text = fromStdin ? std::cin : textFile;

	std::stringstream binary;
	try {
		oms::TextPosition position;
		oms::Section section;
		while(oms::readText(text, section, position))
			binary << section;
	} catch(const oms::ParseError& error) {
		std::cerr << (fromStdin ? "<stdin>" : textPath) << ':' << error.what() << '\n';
		return 1;
	}

	std::ofstream outfile(binaryPath, std::ofstream::binary);
	const std::string_view bytes = binary.view();
	outfile.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	outfile.close();
	if(!outfile) {
		std::cerr << "Cannot write " << binaryPath << '\n';
		return 1;
	}
	return 0;
}

int run(int argc, char** argv) {
	bool listMode = false;
	bool textMode = false;
	bool binaryMode = false;
	bool summaryMode = false;
	bool validArgs = true;
	oms::TextOptions textOptions;
	std::vector<std::string> arguments;

	for(int argIndex = 1; argIndex < argc; ++argIndex) {
		const std::string argument = argv[argIndex];
		if(argument == "--list")
			listMode = true;
		else if(argument == "--text")
			textMode = true;
		else if(argument == "--binary")
			binaryMode = true;
		else if(argument == "--summary" || argument.starts_with("--summary=")) {
			summaryMode = true;
			size_t limit = defaultSummaryLimit;
			if(argument.size() > 9) {
				const char* first = argument.data() + 10;
				const char* last = argument.data() + argument.size();
				const auto [end, errorCode] = std::from_chars(first, last, limit);
				validArgs &= errorCode == std::errc() && end == last;
			}
			textOptions.maxVectorElements = limit;
			textOptions.maxBlobBytes = scaledLimit(limit, 4);
			textOptions.maxArrayElements = scaledLimit(limit, 16);
		} else if(argument.starts_with("--"))
			validArgs = false;
		else
			arguments.push_back(argument);
	}

	validArgs &= (listMode + textMode + binaryMode <= 1) &&
	             (!summaryMode || textMode) &&
	             (listMode   ? arguments.size() == 1 :
	              binaryMode ? arguments.size() == 2 :
	                           arguments.size() == 1 || arguments.size() == 2);
	if(!validArgs) {
		std::cerr << "Usage: omsdump [--list] <file> [section-name]\n"
		             "       omsdump --text [--summary[=N]] <file> [section-name]\n"
		             "       omsdump --binary <text-file> <file>\n"
		             "\n"
		             "  --text     write lossless text, which --binary converts back\n"
		             "  --summary  elide vectors of more than N elements, blobs of more than 4N bytes\n"
		             "             and arrays of more than 16N structures (default N: " << defaultSummaryLimit << ");\n"
		             "             the result cannot be converted back\n"
		             "  --binary   convert text to a binary file; a <text-file> of - reads stdin\n";
		return 1;
	}

	if(binaryMode)
		return convertToBinary(arguments[0], arguments[1]);

	const std::string& path = arguments[0];
	std::ifstream infile(path, std::ifstream::binary);
	if(!infile) {
		std::cerr << "Cannot open " << path << '\n';
		return 1;
	}

	if(listMode) {
		// Print the name (and byte count when available) of each section.
		int sectionIndex = 0;
		while(true) {
			oms::Section section;
			infile >> section;
			if(infile.eof()) break;
			if(!infile) {
				std::cerr << "Error reading " << path << '\n';
				return 1;
			}
			std::cout << sectionIndex++ << ": " << section.name;
			if(section.sectionSize())
				std::cout << " (" << section.sectionSize() << " bytes)";
			std::cout << '\n';
		}
	} else if(arguments.size() == 2) {
		// Find and dump a single named section.
		const std::string& targetName = arguments[1];
		auto result = oms::Section::findNext(infile, targetName);
		if(!result) {
			std::cerr << "Section \"" << targetName << "\" not found in " << path << '\n';
			return 1;
		}
		if(textMode)
			oms::writeText(std::cout, *result, textOptions);
		else
			std::cout << oms::toString(*result) << '\n';
	} else if(textMode) {
		// Write every section in order as text. Unlike the dump below, this stops with an
		// error at a section that is cut short rather than taking it for the end of the file.
		while(infile.peek() != std::ifstream::traits_type::eof()) {
			oms::Section section;
			infile >> section;
			if(!infile) {
				std::cerr << "Error reading " << path << '\n';
				return 1;
			}
			oms::writeText(std::cout, section, textOptions);
		}
	} else {
		// Dump every section in order.
		std::cout << "Dumping " << path << '\n';
		int sectionIndex = 0;
		while(true) {
			oms::Section section;
			infile >> section;
			if(infile.eof()) break;
			if(!infile) {
				std::cerr << "Error reading " << path << '\n';
				return 1;
			}
			std::cout << "Section " << sectionIndex++ << ": " << section.name << '\n';
			std::cout << oms::toString(section) << '\n';
		}
	}

	return 0;
}

int main(int argc, char** argv) {
	// Nothing here mixes C and C++ I/O, and text passes through std::cin and std::cout in bulk.
	std::ios::sync_with_stdio(false);

	try {
		return run(argc, argv);
	} catch(const std::exception& error) {
		std::cerr << "omsdump: " << error.what() << '\n';
		return 1;
	}
}
