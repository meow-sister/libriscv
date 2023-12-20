#include "machine.hpp"

#include <inttypes.h>

namespace riscv
{
	template <int W>
	address_type<W> Memory<W>::elf_base_address(address_t offset) const {
		if (this->m_is_dynamic) {
			const address_t vaddr_base = DYLINK_BASE;
			if (UNLIKELY(vaddr_base + offset < vaddr_base))
				throw MachineException(INVALID_PROGRAM, "Bogus virtual address + offset");
			return vaddr_base + offset;
		} else {
			return offset;
		}
	}

	template <int W>
	const typename Elf<W>::SectionHeader* Memory<W>::section_by_name(const std::string& name) const
	{
		// NOTE: Cannot take address of string_view end-pointer in debug mode
		const char* endptr = m_binary.data() + m_binary.size();

		if (elf_header()->e_shoff > m_binary.size() - sizeof(typename Elf::SectionHeader))
			throw MachineException(INVALID_PROGRAM, "Invalid section header offset");
		const auto* shdr = elf_offset<typename Elf::SectionHeader> (elf_header()->e_shoff);

		const auto& shstrtab = shdr[elf_header()->e_shstrndx];
		if ((const char *)&shstrtab > endptr - sizeof(shstrtab))
			throw MachineException(INVALID_PROGRAM, "Invalid section header offset");

		const char* strings = elf_offset<char>(shstrtab.sh_offset);

		// Only check if the last section header is outside ELF binary,
		// as everything else is further in.
		if ((const char *)&shdr[elf_header()->e_shnum] > endptr)
			throw MachineException(INVALID_PROGRAM, "Invalid ELF string offset");

		for (auto i = 0; i < elf_header()->e_shnum; i++)
		{
			const char* shname = &strings[shdr[i].sh_name];

			if (shname >= endptr)
				throw MachineException(INVALID_PROGRAM, "Invalid ELF string offset");
			const size_t len = strnlen(shname, endptr - shname);
			if (len != name.size())
				continue;

			if (strncmp(shname, name.c_str(), len) == 0) {
				return &shdr[i];
			}
		}
		return nullptr;
	}

	template <int W>
	const typename Elf<W>::Sym* Memory<W>::resolve_symbol(std::string_view name) const
	{
		if (UNLIKELY(m_binary.empty())) return nullptr;
		const auto* sym_hdr = section_by_name(".symtab");
		if (UNLIKELY(sym_hdr == nullptr)) return nullptr;
		const auto* str_hdr = section_by_name(".strtab");
		if (UNLIKELY(str_hdr == nullptr)) return nullptr;
		// ELF with no symbols
		if (UNLIKELY(sym_hdr->sh_size == 0)) return nullptr;

		const auto* symtab = elf_sym_index(sym_hdr, 0);
		const size_t symtab_ents = sym_hdr->sh_size / sizeof(typename Elf::Sym);
		const char* strtab = elf_offset<char>(str_hdr->sh_offset);

		for (size_t i = 0; i < symtab_ents; i++)
		{
			const char* symname = &strtab[symtab[i].st_name];
			if (name.compare(symname) == 0) {
				return &symtab[i];
			}
		}
		return nullptr;
	}


	template <int W>
	static void elf_print_sym(const typename Elf<W>::Sym* sym)
	{
		if constexpr (W == 4) {
			printf("-> Sym is at 0x%" PRIX32 " with size %" PRIu32 ", type %u name %u\n",
				sym->st_value, sym->st_size,
				typename Elf<W>::SymbolType(sym->st_info), sym->st_name);
		} else {
			printf("-> Sym is at 0x%" PRIX64 " with size %" PRIu64 ", type %u name %u\n",
				(uint64_t)sym->st_value, sym->st_size,
				typename Elf<W>::SymbolType(sym->st_info), sym->st_name);
		}
	}

	template <int W> RISCV_INTERNAL
	void Memory<W>::relocate_section(const char* section_name, const char* sym_section)
	{
		using ElfRela = typename Elf::Rela;

		const auto* rela = section_by_name(section_name);
		if (rela == nullptr) return;
		const auto* dyn_hdr = section_by_name(sym_section);
		if (dyn_hdr == nullptr) return;
		const size_t rela_ents = rela->sh_size / sizeof(ElfRela);

		auto* rela_addr = elf_offset<ElfRela>(rela->sh_offset);
		for (size_t i = 0; i < rela_ents; i++)
		{
			size_t symidx;
			if constexpr (W == 4)
				symidx = Elf::RelaSym(rela_addr[i].r_info);
			else
				symidx = Elf::RelaSym(rela_addr[i].r_info);
			auto* sym = elf_sym_index(dyn_hdr, symidx);

			const uint8_t type = Elf::SymbolType(sym->st_info);
			if (type == Elf::STT_FUNC || type == Elf::STT_OBJECT)
			{
				if constexpr (false)
				{
					printf("Relocating rela %zu with sym idx %ld where 0x%lX -> 0x%lX\n",
							i, (long)symidx, (long)rela_addr[i].r_offset, (long)sym->st_value);
					elf_print_sym<W>(sym);
				}
				this->write<address_t>(elf_base_address(rela_addr[i].r_offset), sym->st_value);
			}
		}
	}

	template <int W> RISCV_INTERNAL
	void Memory<W>::dynamic_linking(const typename Elf::Header& hdr)
	{
		(void)hdr;
		this->relocate_section(".rela.dyn", ".dynsym");
		this->relocate_section(".rela.plt", ".dynsym");
	}

	template struct Memory<4>;
	template struct Memory<8>;
	INSTANTIATE_128_IF_ENABLED(Memory);
} // riscv
