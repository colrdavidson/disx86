#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "disx86.h"

typedef struct COFF_SectionHeader {
	char name[8];
	union {
		uint32_t physical_address;
		uint32_t virtual_size;
	} misc;
	uint32_t  virtual_address;
	uint32_t  raw_data_size;
	uint32_t  raw_data_pos;
	uint32_t  pointer_to_reloc;
	uint32_t  pointer_to_lineno;
	uint16_t  num_reloc;
	uint16_t  num_lineno;
	uint32_t  characteristics;
} COFF_SectionHeader;
static_assert(sizeof(COFF_SectionHeader) == 40, "COFF Section header size != 40 bytes");

typedef struct COFF_FileHeader {
	uint16_t machine;
	uint16_t num_sections;
	uint32_t timestamp;
	uint32_t symbol_table;
	uint32_t symbol_count;
	uint16_t optional_header_size;
	uint16_t characteristics;
} COFF_FileHeader;
static_assert(sizeof(COFF_FileHeader) == 20, "COFF File header size != 20 bytes");

static void dissassemble_crap(X86_Buffer input) {
	const uint8_t* start = input.data;

	fprintf(stderr, "error: disassembling %zu bytes...\n", input.length);
	while (input.length > 0) {
		X86_Inst inst;
		X86_Result result = x86_disasm(input, &inst);
		if (result.code != X86_RESULT_SUCCESS) {
			printf("disassembler error: %s (", x86_get_result_string(result.code));
			for (int i = 0; i < result.instruction_length; i++) {
				if (i) printf(" ");
				printf("%x", input.data[i]);
			}
			printf(")\n");

			abort();
		}

		// Print the address
		printf("    %016llX: ", (long long)(input.data - start));

		// Print code bytes
		for (int j = 0; j < 6 && j < result.instruction_length; j++) {
			printf("%02X ", input.data[j]);
		}

		int remaining = result.instruction_length > 6 ? 0 : 6 - result.instruction_length;
		while (remaining--) printf("   ");

		// Print some instruction
		char tmp[32];
		x86_format_inst(tmp, sizeof(tmp), inst.type, inst.data_type);
		printf("%-12s", tmp);

		for (int j = 0; j < inst.operand_count; j++) {
			if (j) printf(",");

			x86_format_operand(tmp, sizeof(tmp), &inst.operands[j], inst.data_type);
			if (inst.operands[j].type == X86_OPERAND_OFFSET) {
				int64_t base_address = (input.data - start)
					+ result.instruction_length;

				printf("%016llX", (long long)(base_address + inst.operands[j].offset));
			} else if (inst.operands[j].type == X86_OPERAND_RIP ||
					   inst.operands[j].type == X86_OPERAND_MEM) {
				printf("%s ptr ", x86_get_data_type_string(inst.data_type));

				if (inst.segment != X86_SEGMENT_DEFAULT) {
					printf("%s:%s", x86_get_segment_string(inst.segment), tmp);
				} else {
					printf("%s", tmp);
				}
			} else {
				printf("%s", tmp);
			}
		}

		printf("\n");

		if (result.instruction_length > 6) {
			printf("                      ");

			size_t j = 6;
			while (j < result.instruction_length) {
				printf("%02X ", input.data[j]);

				if (j && j % 6 == 5) {
					printf("\n");
					printf("                      ");
				}
				j++;
			}

			printf("\n");
		}

		input = x86_advance(input, result.instruction_length);
	}
}

int main(int argc, char* argv[]) {
	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc <= 1) {
		x86_print_dfa_DEBUG();

		fprintf(stderr, "error: no input file!\n");
		return 1;
	}

	bool is_binary = false;
	const char* source_file = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) is_binary = true;
		else {
			if (source_file != NULL) {
				fprintf(stderr, "error: can't hecking open multiple files!\n");
				return 1;
			}

			source_file = argv[i];
		}
	}

	fprintf(stderr, "info: opening %s...\n", source_file);

	// Read sum bites
	FILE* file = fopen(source_file, "rb");
	if (file == NULL) {
		fprintf(stderr, "error: could not open file!\n");
		return 1;
	}

	fseek(file, 0, SEEK_END);
	size_t length = ftell(file);
	rewind(file);

	char* buffer = malloc(length * sizeof(char));
	fread(buffer, length, sizeof(char), file);
	fclose(file);

	if (is_binary) {
		dissassemble_crap((X86_Buffer){ (uint8_t*)buffer, length });
	} else {
		// Locate .text section
		// TODO(NeGate): this isn't properly checked for endianness... i dont care
		// here...
		COFF_FileHeader* file_header = ((COFF_FileHeader*) buffer);
		COFF_SectionHeader* text_section = NULL;
		for (size_t i = 0; i < file_header->num_sections; i++) {
			size_t section_offset = sizeof(COFF_FileHeader) + (i * sizeof(COFF_SectionHeader));
			COFF_SectionHeader* sec = ((COFF_SectionHeader*) &buffer[section_offset]);

			// not very robust because it assumes that the compiler didn't
			// put .text name into a text section and instead did it inplace
			if (strcmp(sec->name, ".text") == 0 ||
				strcmp(sec->name, ".text$mn") == 0) {
				text_section = sec;
				break;
			}
		}

		if (text_section == NULL) {
			fprintf(stderr, "error: could not locate .text section\n");
			abort();
		}

		const uint8_t* text_section_start = (uint8_t*) &buffer[text_section->raw_data_pos];
		dissassemble_crap((X86_Buffer){
							  text_section_start,
							  text_section->raw_data_size
						  });
	}

	return 0;
}
