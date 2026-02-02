/*
    n64rspreloc: patch RSP vector load/store opcodes from custom relocations
	Copyright (C) 2025 Giovanni Bajo (giovannibajo@gmail.com)

    This tool is part of the Libdragon SDK.

    This is free and unencumbered software released into the public domain.
    For more information, please refer to <http://unlicense.org/>
*/
/*
    Background rationale
    --------------------
    RSP vector load/store opcodes (LWC2/SWC2 with vector encodings) encode
    the offset in a signed 7-bit field that is *scaled* (1/2/4/8/16) depending on
    the instruction. The assembler/linker cannot express the required
    operation: take a symbol address, apply a scale, check range, and
    insert the result in the opcode bits. Standard MIPS relocations do
    not match this behavior. This tool performs the missing fixups after
    the ELF is linked.

    High-level flow
    ---------------
    1) The RSP assembler macros (rsp.inc) emit the vector instruction with
       offset 0 and also emit a custom relocation entry in dedicated sections
       called .rspreloc*.
    2) The GCC ELF linker resolves symbols so the immediate field embedded in
       each .rspreloc entry holds the final constant value.
    3) This tool loads the ELF, scans all .rspreloc* sections (including COMDAT
       groups), validates alignment/range, and patches the immediate bits of the
       real instruction in .text using that embedded value.
    4) If a constraint is violated, the tool prints a file:line error by using
       addr2line on the final ELF.

    We need to generate multiple .rspreloc* sections, each one for load/store
    opcodes in each of the various .text* sections, to allow the linker to
    collect unused .text* sections. To make this work, we need to use COMDAT
    groups, binding each .text* section to its own .rspreloc* section, so that
    the linker can discard unused .text* sections together with their
    relocation metadata.

    Relocation section format (.rspreloc / .rspreloc.base / .rspreloc*)
    -------------------------------------------------------------------
    Each entry is a fixed-size struct (rsp_reloc_t) written in big-endian
    order in the ELF:

        uint32_t patch_addr;   // VMA of the instruction to patch (0xA4001000-0xA4001FFF)
        uint16_t scale;        // 1,2,4,8,16 (scale factor used by the opcode)
        uint16_t reserved16;   // must be 0
        uint32_t instr;        // addiu $zero,$zero,IMM (value holder, resolved)
        uint32_t reserved;     // must be 0

    Notes:
    - The immediate value in the instr field is the resolved constant used for
      patching; the tool does not read relocation records.
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>

#include "../common/mips_elf.h"
#include "../common/subprocess.h"

typedef struct {
    uint32_t patch_addr;
    uint16_t scale;
    uint16_t reserved16;
    uint32_t instr;
    uint32_t reserved;
} rsp_reloc_t;

typedef struct {
    char *name;
    Elf32_Addr value;
    Elf32_Half shndx;
    uint8_t info;
} sym_t;

typedef struct {
    FILE *file;
    Elf32_Ehdr ehdr;
    Elf32_Shdr *shdrs;
    char *shstrtab;
    sym_t *syms;
    size_t sym_count;
    char *strtab;
    char *path;
} elf_t;

static bool starts_with(const char *s, const char *prefix);

static const char *g_elf_path = NULL;

static void fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "n64rspreloc: %s: ", g_elf_path ? g_elf_path : "??");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static size_t checked_mul(size_t a, size_t b)
{
    if (a != 0 && b > SIZE_MAX / a)
        fatal("Invalid ELF size (overflow)");
    return a * b;
}

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr)
        fatal("Out of memory allocating %zu bytes", size);
    return ptr;
}

static void *xcalloc(size_t count, size_t size)
{
    size_t total = checked_mul(count, size);
    void *ptr = calloc(1, total);
    if (!ptr)
        fatal("Out of memory allocating %zu bytes", total);
    return ptr;
}

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *ptr = xmalloc(len);
    memcpy(ptr, s, len);
    return ptr;
}

/* Swap a 16-bit value to host endianness. */
static uint16_t bswap16(uint16_t v)
{
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (uint16_t)((v >> 8) | (v << 8));
    #else
    return v;
    #endif
}

/* Swap a 32-bit value to host endianness. */
static uint32_t bswap32(uint32_t v)
{
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(v);
    #else
    return v;
    #endif
}

/* Seek and read a fixed-size block. */
static void read_checked(const elf_t *elf, size_t offset, void *dst, size_t size)
{
    if (fseek(elf->file, offset, SEEK_SET) != 0 || fread(dst, size, 1, elf->file) != 1)
        fatal("Invalid ELF (read failed at 0x%zx)", offset);
}

/* Seek and write a fixed-size block. */
static void write_checked(elf_t *elf, size_t offset, const void *src, size_t size)
{
    if (fseek(elf->file, offset, SEEK_SET) != 0 || fwrite(src, size, 1, elf->file) != 1)
        fatal("Failed to write ELF at 0x%zx", offset);
}

/* Free all ELF resources and close the file. */
static void free_elf(elf_t *elf)
{
    if (!elf) return;
    if (elf->file) fclose(elf->file);
    if (elf->shdrs) free(elf->shdrs);
    if (elf->shstrtab) free(elf->shstrtab);
    if (elf->strtab) free(elf->strtab);
    if (elf->syms) {
        for (size_t i = 0; i < elf->sym_count; i++)
            free(elf->syms[i].name);
        free(elf->syms);
    }
    if (elf->path) free(elf->path);
    free(elf);
}

/* Read and validate the ELF header. */
static void load_elf_header(elf_t *elf)
{
    read_checked(elf, 0, &elf->ehdr, sizeof(elf->ehdr));
    if (memcmp(elf->ehdr.e_ident, ELFMAG, SELFMAG) != 0)
        fatal("Invalid ELF magic");
    if (elf->ehdr.e_ident[EI_CLASS] != ELFCLASS32 ||
        elf->ehdr.e_ident[EI_DATA] != ELFDATA2MSB)
        fatal("Unsupported ELF class/data");

    elf->ehdr.e_type      = bswap16(elf->ehdr.e_type);
    elf->ehdr.e_machine   = bswap16(elf->ehdr.e_machine);
    elf->ehdr.e_version   = bswap32(elf->ehdr.e_version);
    elf->ehdr.e_entry     = bswap32(elf->ehdr.e_entry);
    elf->ehdr.e_phoff     = bswap32(elf->ehdr.e_phoff);
    elf->ehdr.e_shoff     = bswap32(elf->ehdr.e_shoff);
    elf->ehdr.e_flags     = bswap32(elf->ehdr.e_flags);
    elf->ehdr.e_ehsize    = bswap16(elf->ehdr.e_ehsize);
    elf->ehdr.e_phentsize = bswap16(elf->ehdr.e_phentsize);
    elf->ehdr.e_phnum     = bswap16(elf->ehdr.e_phnum);
    elf->ehdr.e_shentsize = bswap16(elf->ehdr.e_shentsize);
    elf->ehdr.e_shnum     = bswap16(elf->ehdr.e_shnum);
    elf->ehdr.e_shstrndx  = bswap16(elf->ehdr.e_shstrndx);

    if (elf->ehdr.e_ehsize != sizeof(Elf32_Ehdr))
        fatal("Invalid ELF header size");
    if (elf->ehdr.e_shentsize != sizeof(Elf32_Shdr))
        fatal("Invalid section header size");
    if (elf->ehdr.e_shoff == 0 || elf->ehdr.e_shnum == 0)
        fatal("Missing section headers");
}

/* Load and byte-swap section headers. */
static void load_section_headers(elf_t *elf)
{
    size_t shdrs_size = checked_mul(elf->ehdr.e_shnum, sizeof(Elf32_Shdr));
    elf->shdrs = xcalloc(elf->ehdr.e_shnum, sizeof(Elf32_Shdr));
    read_checked(elf, elf->ehdr.e_shoff, elf->shdrs, shdrs_size);
    for (Elf32_Half i = 0; i < elf->ehdr.e_shnum; i++) {
        elf->shdrs[i].sh_name      = bswap32(elf->shdrs[i].sh_name);
        elf->shdrs[i].sh_type      = bswap32(elf->shdrs[i].sh_type);
        elf->shdrs[i].sh_flags     = bswap32(elf->shdrs[i].sh_flags);
        elf->shdrs[i].sh_addr      = bswap32(elf->shdrs[i].sh_addr);
        elf->shdrs[i].sh_offset    = bswap32(elf->shdrs[i].sh_offset);
        elf->shdrs[i].sh_size      = bswap32(elf->shdrs[i].sh_size);
        elf->shdrs[i].sh_link      = bswap32(elf->shdrs[i].sh_link);
        elf->shdrs[i].sh_info      = bswap32(elf->shdrs[i].sh_info);
        elf->shdrs[i].sh_addralign = bswap32(elf->shdrs[i].sh_addralign);
        elf->shdrs[i].sh_entsize   = bswap32(elf->shdrs[i].sh_entsize);
    }
}

/* Load the section header string table. */
static void load_shstrtab(elf_t *elf)
{
    if (elf->ehdr.e_shstrndx >= elf->ehdr.e_shnum)
        fatal("Invalid shstrtab index");
    Elf32_Shdr *shstr = &elf->shdrs[elf->ehdr.e_shstrndx];
    if (shstr->sh_type != SHT_STRTAB)
        fatal("Invalid shstrtab section");
    elf->shstrtab = xmalloc(shstr->sh_size);
    read_checked(elf, shstr->sh_offset, elf->shstrtab, shstr->sh_size);
}

/* Find a section header by name. */
static Elf32_Shdr *find_section(elf_t *elf, const char *name)
{
    for (Elf32_Half i = 0; i < elf->ehdr.e_shnum; i++) {
        const char *sname = elf->shstrtab + elf->shdrs[i].sh_name;
        if (strcmp(sname, name) == 0)
            return &elf->shdrs[i];
    }
    return NULL;
}

/* Load .symtab and associated string table. */
static void load_symtab(elf_t *elf)
{
    Elf32_Shdr *symtab = find_section(elf, ".symtab");
    if (!symtab)
        fatal("Missing .symtab");
    if (symtab->sh_entsize != sizeof(Elf32_Sym))
        fatal("Invalid .symtab entry size");
    if (symtab->sh_link >= elf->ehdr.e_shnum)
        fatal("Invalid .symtab string table link");
    Elf32_Shdr *strtab = &elf->shdrs[symtab->sh_link];
    if (strtab->sh_type != SHT_STRTAB)
        fatal("Invalid .symtab string table");
    elf->strtab = xmalloc(strtab->sh_size);
    read_checked(elf, strtab->sh_offset, elf->strtab, strtab->sh_size);

    if (symtab->sh_size % sizeof(Elf32_Sym))
        fatal("Invalid .symtab size");
    elf->sym_count = symtab->sh_size / sizeof(Elf32_Sym);
    elf->syms = xcalloc(elf->sym_count, sizeof(sym_t));

    for (size_t i = 0; i < elf->sym_count; i++) {
        Elf32_Sym sym;
        size_t off = symtab->sh_offset + i * sizeof(Elf32_Sym);
        read_checked(elf, off, &sym, sizeof(sym));
        sym.st_name  = bswap32(sym.st_name);
        sym.st_value = bswap32(sym.st_value);
        sym.st_size  = bswap32(sym.st_size);
        sym.st_shndx = bswap16(sym.st_shndx);

        if (sym.st_name >= strtab->sh_size)
            fatal("Invalid symbol name offset");
        elf->syms[i].name = xstrdup(elf->strtab + sym.st_name);
        elf->syms[i].value = sym.st_value;
        elf->syms[i].shndx = sym.st_shndx;
        elf->syms[i].info = sym.st_info;
    }
}

/*
    Score a symbol for fallback name selection.
    Needed because multiple symbols can share the same value/low16 (aliases,
    section symbols, local labels). We prefer global/weak + func/object names,
    and reject file symbols or internal helper names.
*/
static int sym_score(const sym_t *sym)
{
    if (!sym || !sym->name || !sym->name[0])
        return -1;
    if (starts_with(sym->name, "__rsp_") || starts_with(sym->name, "__"))
        return -1;
    int score = 0;
    unsigned bind = ELF32_ST_BIND(sym->info);
    unsigned type = ELF32_ST_TYPE(sym->info);
    if (type == STT_FILE)
        return -1;
    if (bind == STB_GLOBAL || bind == STB_WEAK)
        score += 3;
    else if (bind == STB_LOCAL)
        score += 1;
    if (type == STT_FUNC || type == STT_OBJECT)
        score += 2;
    if (sym->name[0] != '.' && sym->name[0] != '$')
        score += 1;
    return score;
}

/* Find the best symbol matching an exact value. */
static const sym_t *find_sym_by_value(const elf_t *elf, Elf32_Addr value)
{
    const sym_t *best = NULL;
    int best_score = -1;
    for (size_t i = 0; i < elf->sym_count; i++) {
        const sym_t *sym = &elf->syms[i];
        if (sym->shndx == SHN_UNDEF)
            continue;
        if (sym->value != value)
            continue;
        int score = sym_score(sym);
        if (score > best_score) {
            best = sym;
            best_score = score;
        }
    }
    return best;
}

/* Open an ELF and load headers/sections/symbols. */
static elf_t *load_elf(const char *path, bool writeable)
{
    g_elf_path = path;
    elf_t *elf = xcalloc(1, sizeof(*elf));
    elf->path = xstrdup(path);
    elf->file = fopen(path, writeable ? "r+b" : "rb");
    if (!elf->file) {
        free_elf(elf);
        fatal("Cannot open ELF: %s", strerror(errno));
    }
    load_elf_header(elf);
    load_section_headers(elf);
    load_shstrtab(elf);
    load_symtab(elf);
    return elf;
}

/* Check if instruction is vector LWC2/SWC2. */
static bool is_vector_ls(uint32_t instr)
{
    uint32_t op = instr >> 26;
    return (op == 0x32 || op == 0x3A);
}

/* Resolve the addr2line binary path. */
static const char *addr2line_bin(const char *elf_path)
{
    static char *addrbin = NULL;
    if (addrbin) return addrbin;
    const char *n64inst = getenv("N64_INST");
    if (n64inst) {
        size_t len = (size_t)snprintf(NULL, 0, "%s/bin/mips64-elf-addr2line", n64inst) + 1;
        addrbin = xmalloc(len);
        snprintf(addrbin, len, "%s/bin/mips64-elf-addr2line", n64inst);
    } else {
        addrbin = xstrdup("mips64-elf-addr2line");
    }
    return addrbin;
}

/* Translate an address to file:line with addr2line. */
static bool addr_to_line(const char *elf_path, uint32_t addr, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;
    snprintf(out, out_size, "??:0");
    if (!elf_path)
        return false;

    static struct subprocess_s subp;
    static FILE *addr2line_w = NULL, *addr2line_r = NULL;
    static const char *cur_elf = NULL;
    static char *line_buf = NULL;
    static size_t line_buf_size = 0;

    if (!cur_elf || strcmp(cur_elf, elf_path)) {
        if (cur_elf) {
            subprocess_terminate(&subp);
            cur_elf = NULL; addr2line_w = addr2line_r = NULL;
        }
        const char *cmd_addr[8] = {0}; int i = 0;
        cmd_addr[i++] = addr2line_bin(elf_path);
        cmd_addr[i++] = "--functions";
        cmd_addr[i++] = "--exe";
        cmd_addr[i++] = elf_path;

        if (subprocess_create(cmd_addr, subprocess_option_no_window, &subp) != 0) {
            return false;
        }
        addr2line_w = subprocess_stdin(&subp);
        addr2line_r = subprocess_stdout(&subp);
        cur_elf = elf_path;
    }

    fprintf(addr2line_w, "%08x\n", addr);
    fflush(addr2line_w);

    if (getline(&line_buf, &line_buf_size, addr2line_r) <= 0)
        return false; // function name (ignored)
    if (getline(&line_buf, &line_buf_size, addr2line_r) <= 0)
        return false; // file:line

    size_t len = strlen(line_buf);
    if (len && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r'))
        line_buf[--len] = '\0';
    if (len && line_buf[len - 1] == '\r')
        line_buf[--len] = '\0';
    snprintf(out, out_size, "%s", line_buf);
    return true;
}

/* Print a formatted message with source location. */
static void print_msg_at(const char *elf_path, uint32_t addr, const char *level, const char *sym_name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char loc[256];
    addr_to_line(elf_path, addr, loc, sizeof(loc));
    fprintf(stderr, "%s: %s: ", loc, level);
    vfprintf(stderr, fmt, args);
    if (sym_name && sym_name[0])
        fprintf(stderr, " (symbol %s)", sym_name);
    fprintf(stderr, "\n");
    va_end(args);
}

#define print_error_at(elf_path, addr, sym_name, fmt, ...) print_msg_at(elf_path, addr, "error", sym_name, fmt, ##__VA_ARGS__)
#define print_info_at(elf_path, addr, sym_name, fmt, ...)  print_msg_at(elf_path, addr, "info", sym_name, fmt, ##__VA_ARGS__)

/* Compute file offset for a VMA inside .text. */
static uint32_t locate_instr_at(const elf_t *elf, uint32_t patch_addr)
{
    Elf32_Shdr *text = NULL;
    for (Elf32_Half i = 0; i < elf->ehdr.e_shnum; i++) {
        Elf32_Shdr *sh = &elf->shdrs[i];
        if (patch_addr >= sh->sh_addr && patch_addr + 4 <= sh->sh_addr + sh->sh_size) {
            text = sh;
            break;
        }
    }
    if (!text)
        fatal("Invalid relocation address 0x%08x", patch_addr);
    return text->sh_offset + (patch_addr - text->sh_addr);
}

/* Patch the 7-bit immediate of a vector load/store. */
static bool patch_instr(elf_t *elf, const char *elf_path, uint32_t patch_addr, int32_t scaled, uint16_t scale)
{
    if (scaled < -64 || scaled > 63) {
        print_error_at(elf_path, patch_addr, NULL, "Invalid offset - valid range: [%d, %d]", -64 * (int)scale, 63 * (int)scale);
        return false;
    }
    uint32_t encoded = (scaled >= 0) ? (uint32_t)scaled : (uint32_t)(128 + scaled);

    uint32_t file_off = locate_instr_at(elf, patch_addr);
    uint32_t instr_be;
    read_checked(elf, file_off, &instr_be, sizeof(instr_be));
    uint32_t instr = bswap32(instr_be);
    if (!is_vector_ls(instr)) {
        print_error_at(elf_path, patch_addr, NULL, "Instruction is not LWC2/SWC2");
        return false;
    }
    uint32_t imm = instr & 0xFFFF;
    imm = (imm & ~0x7F) | (encoded & 0x7F);
    instr = (instr & 0xFFFF0000) | imm;
    instr_be = bswap32(instr);
    write_checked(elf, file_off, &instr_be, sizeof(instr_be));
    return true;
}

/* Return true if s starts with prefix. */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Read a 32-bit instruction at a VMA. */
static uint32_t read_instr_at(const elf_t *elf, uint32_t patch_addr)
{
    uint32_t file_off = locate_instr_at(elf, patch_addr);
    uint32_t instr_be;
    read_checked(elf, file_off, &instr_be, sizeof(instr_be));
    return bswap32(instr_be);
}

/* Format an unsigned value as 12-bit hex. */
static void format_hex12(char *buf, size_t buf_size, uint64_t value)
{
    snprintf(buf, buf_size, "0x%03" PRIx64, value & 0xFFF);
}

/* Format a signed value as 12-bit hex with sign. */
static void format_signed_hex12(char *buf, size_t buf_size, int64_t value)
{
    if (value < 0) {
        uint64_t absval = (uint64_t)(-value);
        snprintf(buf, buf_size, "-0x%03" PRIx64, absval & 0xFFF);
    } else {
        format_hex12(buf, buf_size, (uint64_t)value);
    }
}

/* Process one .rspreloc* section and apply patches. */
static void process_rspreloc_section(elf_t *elf, const char *elf_path, Elf32_Half sec_index, bool verbose)
{
    Elf32_Shdr *rspreloc = &elf->shdrs[sec_index];

    if (rspreloc->sh_size % sizeof(rsp_reloc_t) != 0)
        fatal("Invalid .rspreloc entry size");

    size_t entry_count = rspreloc->sh_size / sizeof(rsp_reloc_t);
    if (entry_count == 0)
        return;

    /* Load and byte-swap relocation entries. */
    rsp_reloc_t *entries = xcalloc(entry_count, sizeof(rsp_reloc_t));
    read_checked(elf, rspreloc->sh_offset, entries, rspreloc->sh_size);
    for (size_t i = 0; i < entry_count; i++) {
        entries[i].patch_addr = bswap32(entries[i].patch_addr);
        entries[i].scale = bswap16(entries[i].scale);
        entries[i].reserved16 = bswap16(entries[i].reserved16);
        entries[i].instr = bswap32(entries[i].instr);
        entries[i].reserved = bswap32(entries[i].reserved);
    }

    /* Apply patches. */
    for (size_t i = 0; i < entry_count; i++) {
        uint16_t scale = entries[i].scale;
        if (!(scale == 1 || scale == 2 || scale == 4 || scale == 8 || scale == 16)) {
            fprintf(stderr, "error: invalid scale %u in rspreloc\n", scale);
            continue;
        }

        int64_t min_off = -64 * (int64_t)scale;
        int64_t max_off = 63 * (int64_t)scale;

        /* Resolve the value to patch from the instruction immediate. */
        int64_t sym_value = (int64_t)(int16_t)(entries[i].instr & 0xFFFF);
        int32_t scaled = (int32_t)(sym_value / scale);

        /* Symbol lookup is only useful if we're going to print an error message. */
        bool must_lookup_sym = verbose ||
            sym_value % scale != 0 || sym_value < min_off || sym_value > max_off;

        /* Do symbol lookup if needed. */
        char off_str[32]; bool zero_base = false;
        const char *err_sym = NULL;
        if (must_lookup_sym) {
            const char *sym_name = NULL;
            const sym_t *match = find_sym_by_value(elf, (Elf32_Addr)sym_value + 0xA4000000);
            if (match)
                sym_name = match->name;

            /* Prepare the error message. */
            format_signed_hex12(off_str, sizeof(off_str), sym_value);
            uint32_t patch_instr_word = read_instr_at(elf, entries[i].patch_addr);
            uint8_t base_reg = (uint8_t)((patch_instr_word >> 21) & 0x1F);
            zero_base = base_reg == 0;
            err_sym = zero_base ? sym_name : NULL;
        }

        /* Check scale alignment and range; format errors by base register case. */
        if (sym_value % scale) {
            print_error_at(elf_path, entries[i].patch_addr, err_sym,
                "Invalid offset %s - must be multiple of %u",
                off_str, scale);
            continue;
        }

        if (sym_value < min_off || sym_value > max_off) {
            if (zero_base) {
                uint64_t wrap_start = (uint64_t)(0x1000 + min_off);
                uint64_t wrap_end = 0x1000;
                uint64_t low_start = 0;
                uint64_t low_end = (uint64_t)max_off;
                char low_start_s[16], low_end_s[16], wrap_start_s[16], wrap_end_s[16];
                format_hex12(low_start_s, sizeof(low_start_s), low_start);
                format_hex12(low_end_s, sizeof(low_end_s), low_end);
                format_hex12(wrap_start_s, sizeof(wrap_start_s), wrap_start);
                format_hex12(wrap_end_s, sizeof(wrap_end_s), wrap_end);
                print_error_at(elf_path, entries[i].patch_addr, err_sym,
                    "Invalid offset %s - valid DMEM ranges: [%s, %s] or [%s, %s]",
                    off_str, low_start_s, low_end_s, wrap_start_s, wrap_end_s);
            } else {
                char min_s[32], max_s[32];
                format_signed_hex12(min_s, sizeof(min_s), min_off);
                format_signed_hex12(max_s, sizeof(max_s), max_off);
                print_error_at(elf_path, entries[i].patch_addr, err_sym,
                    "Invalid offset %s - valid range: [%s, %s]",
                    off_str, min_s, max_s);
            }
            continue;
        }

        /* Proceed to patch the instruction. */
        if (verbose) {
            print_info_at(elf_path, entries[i].patch_addr, err_sym,
                "patch 0x%08x scale=%u sym=0x%08" PRIx64,
                entries[i].patch_addr, scale, (uint64_t)sym_value);
        }
        patch_instr(elf, elf_path, entries[i].patch_addr, scaled, scale);
    }

    free(entries);
}

/* Print command-line usage. */
static void usage(const char *name)
{
    fprintf(stderr, "n64rspreloc: apply custom relocations to RSP ELF files\n\n");
    fprintf(stderr, "Usage: %s [-v] <rsp.elf>\n", name);
    fprintf(stderr, "   -v               Verbose output\n");
}

/* Entry point: parse args and patch ELF. */
int main(int argc, char **argv)
{
    const char *elf_path = NULL;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        } else {
            elf_path = argv[i];
        }
    }
    if (!elf_path) {
        usage(argv[0]);
        return 1;
    }

    elf_t *elf = load_elf(elf_path, true);

    int processed = 0;
    for (Elf32_Half i = 0; i < elf->ehdr.e_shnum; i++) {
        const char *sname = elf->shstrtab + elf->shdrs[i].sh_name;
        if (starts_with(sname, ".rspreloc")) {
            processed++;
            process_rspreloc_section(elf, elf_path, i, verbose);
        }
    }
    if (processed == 0 && verbose)
        fprintf(stderr, "n64rspreloc: no .rspreloc in %s\n", elf_path);

    free_elf(elf);
    return 0;
}
