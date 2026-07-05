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
#include "OmsString.hpp"

int main(int argc, char** argv) {
	if(argc < 2 || argc > 3) {
		std::cerr << "Usage: odump <file> [section-name]\n";
		return 1;
	}

	const char* path = argv[1];
	std::ifstream infile(path, std::ifstream::binary);
	if(!infile) {
		std::cerr << "Cannot open " << path << '\n';
		return 1;
	}

	if(argc == 3) {
		// Find and dump a single named section.
		const std::string targetName = argv[2];
		auto result = oms::Section::findNext(infile, targetName);
		if(!result) {
			std::cerr << "Section \"" << targetName << "\" not found in " << path << '\n';
			return 1;
		}
		std::cout << oms::toString(*result) << '\n';
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
