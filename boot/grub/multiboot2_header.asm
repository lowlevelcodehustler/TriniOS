; ───────────────────────────────────────────────────────────────────────────
; multiboot2_header.asm
; TriniOS Multiboot2 Header for GRUB2
; ───────────────────────────────────────────────────────────────────────────
;
; This file defines the Multiboot2 header that GRUB2 scans for at kernel load
; time. GRUB2 searches the first 32 KiB of the kernel image for the magic
; signature. When found, it reads the associated tags to determine how to
; load the kernel and initrd.
;
; Multiboot2 Specification:
; https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
;
; ───────────────────────────────────────────────────────────────────────────

; ── Alignment & visibility ──────────────────────────────────────────────────
    .text
    .align 8
    .globl multiboot2_header

; ── Multiboot2 Header Start Marker ─────────────────────────────────────────
multiboot2_header:
    ; ── Field: total header size (bytes) ────────────────────────────────────
    ; Must be large enough to cover all tags including the terminator.
    ; Layout below = 8 (total) + 8 (type+flags+size) * 4 tags + 8 (terminator)
    ; = 8 + 32 + 8 = 48 bytes
    .long   48                  ; total_header_size

    ; ── Field: reserved (must be aligned) ───────────────────────────────────
    ; checksum = ~(magic + total_header_size + reserved) & 0xFFFFFFFF
    ; We compute: magic=0xE8520PF0, total=48, reserved=0
    ; checksum = ~(0xE8520PF0 + 48 + 0) = ~0xE8520P40 = 0x17ADFFBF
    ; But GRUB2 does NOT require the checksum field (that was Multiboot1).
    ; Multiboot2 uses: (magic + total + reserved + checksum) mod 2^32 = 0
    ; So: checksum = -(magic + total + reserved) mod 2^32
    .long   0xE8520PF0          ; magic
    .long   0x17ADFFBF          ; checksum (computed above, adjusted below)

; ── Tag 1: Required Header Tag (type=1) ─────────────────────────────────────
    ; Indicates this is a required header (always present)
    .short  1                   ; tag_type  (REQUIRED_HEADER_TAG)
    .short  0                   ; tag_flags
    .long   8                   ; tag_size  (8 bytes, no data)

; ── Tag 2: Entry Address Tag (type=5) ──────────────────────────────────────
    ; Tells GRUB2 where to transfer control after loading
    .short  5                   ; tag_type  (ENTRY_ADDRESS_TAG)
    .short  0                   ; tag_flags
    .long   24                  ; tag_size  (24 bytes = 8 header + 16 data)
    .long   0                   ; header_addr_hi (32-bit high part, x86_64 uses low)
    .long   0                   ; load_addr_hi
    .quad   _start              ; entry_addr (64-bit entry point address)

; ── Tag 3: Load Address Tag (type=6) ───────────────────────────────────────
    ; Tells GRUB2 the load address & end address
    .short  6                   ; tag_type  (LOAD_ADDRESS_TAG)
    .short  0                   ; tag_flags
    .long   24                  ; tag_size
    .quad   _start              ; load_addr_addr (where kernel should be loaded)
    .quad   _end                ; load_end_addr (end of kernel image)

; ── Tag 4: Init Array Tag (type=18) - Optional, for C constructors ──────────
    .short  18                  ; tag_type  (INIT_ARRAY_TAG)
    .short  0                   ; tag_flags
    .long   24                  ; tag_size
    .quad   0                   ; address of init array
    .long   0                   ; size of init array
    .long   0                   ; alignment

; ── Tag 0: Terminator (type=0) ─────────────────────────────────────────────
    .short  0                   ; tag_type  (TERMINAL_TAG)
    .short  0                   ; tag_flags
    .long   8                   ; tag_size

; ── Pad to ensure the header is within first 32 KiB ─────────────────────────
    .align 8

; ───────────────────────────────────────────────────────────────────────────
; End of multiboot2 header
; ───────────────────────────────────────────────────────────────────────────