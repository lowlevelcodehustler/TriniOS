; ───────────────────────────────────────────────────────────────────────────
; kernel_entry.asm
; TriniOS x86_64 Kernel Entry Point — 32-bit to 64-bit Long Mode Transition
; ───────────────────────────────────────────────────────────────────────────
;
; GRUB2 hands control to us in 32-bit protected mode with:
;   EAX = 0x36D76289 (Multiboot2 magic)
;   EBX = pointer to Multiboot2 info structure
;   CR0.PG = 0 (paging disabled)
;   A20 line enabled
;   Interrupts disabled
;   CPUs other than boot CPU in stopped state
;
; We must:
;   1. Set up a temporary stack
;   2. Enable PAE + long mode via MSR writes
;   3. Load a 4-level page table identity-mapping low memory
;   4. Load GDT (64-bit descriptors)
;   5. Jump to 64-bit code
;   6. Set up 64-bit stack, clear BSS, call C entry point
;
; ───────────────────────────────────────────────────────────────────────────

    .text
    .code32
    .globl _start

; ───────────────────────────────────────────────────────────────────────────
; _start: GRUB2 entry point (32-bit protected mode)
; ───────────────────────────────────────────────────────────────────────────
_start:
    cli                     ; ensure interrupts off
    cld                     ; forward string direction

    ; ── Preserve multiboot info pointer (EBX) across transition ────────────
    mov     eax, ebx        ; save multiboot info ptr in EAX temporarily
    mov     [mb_info_ptr], eax

    ; ── Set up temporary 32-bit stack ──────────────────────────────────────
    mov     esp, stack32_top

    ; ── Parse multiboot2 info: extract memory map & initrd info ────────────
    call    parse_multiboot2_info

    ; ── Enable Physical Address Extension (PAE) ────────────────────────────
    mov     eax, cr0
    bts     eax, 5          ; set CR0.PAE = 1
    mov     cr0, eax

    ; ── Set up 4-level page tables for identity mapping ────────────────────
    ; We need to map at least the first 1 GiB for kernel operation
    mov     eax, page_table_base
    mov     cr3, eax        ; load page directory base

    ; ── Enable Long Mode via EFER MSR ──────────────────────────────────────
    mov     ecx, 0xC0000080 ; EFER MSR address
    rdmsr
    bts     eax, 8          ; set EFER.LME = 1 (Long Mode Enable)
    wrmsr

    ; ── Enable Paging (enters long mode) ───────────────────────────────────
    mov     eax, cr0
    bts     eax, 31         ; set CR0.PG = 1
    mov     cr0, eax

    ; ── Load 64-bit GDT ───────────────────────────────────────────────────
    lgdt    [gdt64_ptr]

    ; ── Enable Non-Segment-Limit Long Mode (EFER.LMA) ──────────────────────
    ; After setting PG with LME=1, the CPU enters long mode
    ; We need to do a far jump to flush the CS cache

    ; ── Far jump to 64-bit code segment (GDT index 1 = code64) ────────────
    ; Use a code pointer: offset=enter_long_mode, selector=0x08 (GDT entry 1)
    jmp     $0x08, $enter_long_mode

; ───────────────────────────────────────────────────────────────────────────
; enter_long_mode: now executing in 64-bit mode
; ───────────────────────────────────────────────────────────────────────────
enter_long_mode:
    .code64

    ; ── Set up 64-bit segment registers ────────────────────────────────────
    mov     ax, 0x10        ; data segment selector (GDT entry 2)
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax

    ; ── Set up 64-bit stack ───────────────────────────────────────────────
    mov     rsp, stack64_top

    ; ── Clear BSS ─────────────────────────────────────────────────────────
    xor     eax, eax
    mov     edi, bss_start
    mov     ecx, bss_end
    sub     ecx, edi
    rep     stosb

    ; ── Retrieve multiboot info pointer and call C kernel entry ────────────
    mov     rdi, [mb_info_ptr]    ; mb_info pointer as first argument
    call    kernel_main           ; C entry point: void kernel_main(uint64_t mb_info)

    ; ── If kernel_main returns, halt ──────────────────────────────────────
.halt:
    cli
    hlt
    jmp     .halt

; ───────────────────────────────────────────────────────────────────────────
; parse_multiboot2_info: walk the multiboot2 tag list
; Extract: initrd load addr/size, memory map, kernel load addr
; ───────────────────────────────────────────────────────────────────────────
parse_multiboot2_info:
    push    rbx
    push    rdi
    push    rsi

    mov     edi, [mb_info_ptr]    ; edi = mb_info structure
    mov     esi, [edi]            ; total size of mb_info (first 32 bits)

    ; Walk tags: start after the total_size field
    add     edi, 8                ; skip total_size + reserved fields (16 bytes)
    sub     esi, 8                ; account for bytes already skipped

parse_tag_loop:
    test    esi, esi
    jz      parse_done            ; walked entire structure

    mov     ax, [edi]             ; tag type
    mov     bx, [edi + 4]         ; tag size (includes 4-byte alignment padding)

    ; Check for tags we care about:
    ; Type 5: Load Base Overlays (skip)
    ; Type 6: Entry Address (skip, we handle this ourselves)
    ; Type 9: Boot Loader Name
    ; Type 10: ELF Sections
    ; Type 11: APM Table
    ; Type 12: Video Info
    ; Type 13: Boot Loader Timer (type 9)
    ; Type 1: Required Header (skip)
    ; Type 7: Framebuffer Info
    ; Type 4: Module (initrd is loaded as a module in MB2)
    ; Type 8: Basic Mem Info
    ; Type 14: Memory Map
    ; Type 15: VGA State
    ; Type 16: Drive Info
    ; Type 17: Table of Contents
    ; Type 19: EFI 32-bit
    ; Type 20: EFI 64-bit
    ; Type 21: SMP CPUs
    ; Type 22: ACPI

    cmp     ax, 4                 ; MODULE_TAG (initrd loaded as module)
    je      handle_module_tag
    cmp     ax, 14                ; MEMORY_MAP_TAG
    je      handle_mmap_tag
    cmp     ax, 12                ; VIDEO_INFO_TAG
    je      handle_video_tag

    ; Skip this tag
    add     edi, bx
    sub     esi, bx
    jmp     parse_tag_loop

handle_module_tag:
    ; Module tag: offset 8 = mod_start, offset 16 = mod_end, offset 24 = cmdline
    mov     eax, [edi + 8]
    mov     [initrd_load_addr], eax
    mov     eax, [edi + 16]
    mov     [initrd_load_end], eax
    ; Compute size = end - start
    mov     eax, [initrd_load_end]
    sub     eax, [initrd_load_addr]
    mov     [initrd_load_size], eax
    ; Continue walking tags
    add     edi, bx
    sub     esi, bx
    jmp     parse_tag_loop

handle_mmap_tag:
    ; Memory map tag: offset 8 = mmap_entries, offset 12 = mmap_entry_size
    mov     eax, [edi + 8]
    mov     [mmap_entry_count], eax
    mov     ax, [edi + 12]
    mov     [mmap_entry_size], ax
    ; mmap data starts at offset 16
    mov     eax, edi
    add     eax, 16
    mov     [mmap_addr], eax
    add     edi, bx
    sub     esi, bx
    jmp     parse_tag_loop

handle_video_tag:
    ; Video info tag (type 12): framebuffer info
    mov     eax, [edi + 8]
    mov     [video_mode], eax
    mov     eax, [edi + 12]
    mov     [video_width], eax
    mov     eax, [edi + 16]
    mov     [video_height], eax
    mov     eax, [edi + 20]
    mov     [video_depth], eax
    mov     eax, [edi + 24]
    mov     [video_addr], eax
    add     edi, bx
    sub     esi, bx
    jmp     parse_tag_loop

parse_done:
    pop     rsi
    pop     rdi
    pop     rbx
    ret

; ───────────────────────────────────────────────────────────────────────────
; GDT — 32-bit (used before long mode switch)
; ───────────────────────────────────────────────────────────────────────────
gdt32_start:
    .quad   0                     ; null descriptor
    .word   0xFFFF                ; code limit (low 16)
    .word   0                     ; code base (low 16)
    .byte   0                     ; code base (byte 2)
    .byte   0x9A                  ; code access (present, r/w, conformed)
    .byte   0xCF                  ; code granularity + flags
    .byte   0                     ; code base (high byte)
    .word   0xFFFF                ; data limit (low 16)
    .word   0                     ; data base (low 16)
    .byte   0                     ; data base (byte 2)
    .byte   0x92                  ; data access (present, r/w, expand-down)
    .byte   0xCF                  ; data granularity + flags
    .byte   0                     ; data base (high byte)
gdt32_end:

gdt32_ptr:
    .word   gdt32_end - gdt32_start - 1   ; limit
    .long   gdt32_start                   ; base

; ───────────────────────────────────────────────────────────────────────────
; GDT — 64-bit (loaded before paging, used in long mode)
; ───────────────────────────────────────────────────────────────────────────
gdt64_start:
    .quad   0                     ; null descriptor
    ; Code segment: present, ring 0, code, execute-read, 64-bit, long mode
    .word   0xFFFF                ; limit low
    .word   0                     ; base low
    .byte   0                     ; base mid
    .byte   0xA8                  ; access: present, ring 0, code, exec-read
    .byte   0x20                  ; flags: 64-bit, no segmentation
    .byte   0                     ; base high
    ; Data segment: present, ring 0, data, read-write
    .word   0xFFFF                ; limit low
    .word   0                     ; base low
    .byte   0                     ; base mid
    .byte   0x88                  ; access: present, ring 0, data, rw
    .byte   0xCF                  ; flags: 32-bit, granularity 4K
    .byte   0                     ; base high
    .quad   0                     ; padding for alignment
gdt64_end:

gdt64_ptr:
    .word   gdt64_end - gdt64_start - 1   ; limit
    .quad   gdt64_start                   ; base (64-bit for LGDT in long mode)

; ───────────────────────────────────────────────────────────────────────────
; 4-Level Page Tables (identity map first 1 GiB)
; PML4 -> PDPT -> PD -> PT
; Each table = 4096 entries * 8 bytes = 32 KiB
; We map 1 GiB = 256 pages per PT * 512 PTs = 1 GiB
; ───────────────────────────────────────────────────────────────────────────
;
; Page table layout in memory:
;   page_table_pml4     [4096 bytes]  — PML4 (level 4)
;   page_table_pdpt     [4096 bytes]  — PDPT (level 3)
;   page_table_pd       [4096 bytes]  — PD (level 2)
;   page_table_pt       [4096 bytes]  — PT (level 1)
;
; We identity-map the first 2 MiB using a 2-MiB page (huge page) at level 2,
; so we only need PML4[0] -> PDPT[0] -> PD[0] with PS=1 (2-MiB page).
; This maps physical 0..2MiB to virtual 0..2MiB, covering our kernel.
;
; Flags: present (bit 0), writable (bit 1), supervisor (bit 2),
;        write-through=0, nocache=0, accessed=1, NX=0
; Standard flags = 0x27 (P+W+U=0+AT+WT=0+NC=0+ACC=1)
; For 2-MiB page: add PS (bit 7) = 0x27 | 0x80 = 0xA7
; ───────────────────────────────────────────────────────────────────────────
page_table_base:
    ; PML4: entry 0 -> PDPT (covers virtual 0..512GiB with one entry)
    .quad   page_table_pdpt - page_table_base + 0x27

    ; fill remaining 511 entries with zero (not mapped)
    .fill   511, 8, 0

page_table_pdpt:
    ; PDPT: entry 0 -> PD (covers virtual 0..1GiB)
    .quad   page_table_pd - page_table_base + 0x27

    .fill   511, 8, 0

page_table_pd:
    ; PD: entry 0 -> 2-MiB huge page mapping physical 0..2MiB
    .quad   0xA7  ; present, writable, accessed, PS=1 (2-MiB page), base=0

    .fill   511, 8, 0

page_table_pt:
    ; Not needed for 2-MiB mapping, but reserve space
    .fill   512, 8, 0

; ───────────────────────────────────────────────────────────────────────────
; Data: multiboot2 info parsed results
; ───────────────────────────────────────────────────────────────────────────
    .align 8
mb_info_ptr:
    .quad   0

initrd_load_addr:
    .long   0
initrd_load_end:
    .long  0
initrd_load_size:
    .long  0

mmap_entry_count:
    .long  0
mmap_entry_size:
    .word  0
mmap_addr:
    .long  0

video_mode:
    .long  0
video_width:
    .long  0
video_height:
    .long  0
video_depth:
    .long  0
video_addr:
    .long  0

; ───────────────────────────────────────────────────────────────────────────
; Stacks
; ───────────────────────────────────────────────────────────────────────────
    .align 12
stack32_start:
    .space  4096
stack32_top:

    .align 12
stack64_start:
    .space  16384
stack64_top:

; ───────────────────────────────────────────────────────────────────────────
; BSS markers
; ───────────────────────────────────────────────────────────────────────────
    .globl bss_start
bss_start:
    .space 256
    .globl bss_end
bss_end:

; ───────────────────────────────────────────────────────────────────────────
; Mark end of kernel image for GRUB2 load address tag
; ───────────────────────────────────────────────────────────────────────────
    .globl _end
_end:
    .quad  0

; ───────────────────────────────────────────────────────────────────────────
; End of kernel_entry.asm
; ───────────────────────────────────────────────────────────────────────────