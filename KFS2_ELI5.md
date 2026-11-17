# KFS_2 - What was added, explained like you're 5

This file explains every piece of code that was **added or changed** to turn the KFS_1 kernel into a KFS_2 kernel (GDT + stack + bonus shell).

---

## 0. The checklist from the subject

| Subject requirement | Where it is done |
|---|---|
| Create a GDT | `kernel/gdt.c`, `include/gdt.h` |
| GDT contains Kernel Code / Data / Stack, User Code / Data / Stack | `gdt_init()` in `kernel/gdt.c` (7 entries incl. the mandatory Null one) |
| Declare the GDT to the CPU ("to the BIOS") | `lgdt` in `boot/gdt_flush.s` |
| GDT at address `0x00000800` | `gdt_location = 0x00000800;` in `linker.ld` |
| Work under 10 MB | ISO stays ~3.5 MB (GRUB fonts/locales/themes already stripped in the Makefile) |
| Tool to print the kernel stack in a human-friendly way | `print_kernel_stack()` in `kernel/stack.c` (+ `kernel/symbols.c`) |
| **Bonus**: a small debug shell (print stack, reboot, halt…) | `kernel/shell.c`, `kernel/power.c` |

### Files at a glance

| File | Status | One-line summary |
|---|---|---|
| `include/gdt.h` | **new** | The "shape" of a GDT entry, the selector numbers, the access bits |
| `kernel/gdt.c` | **new** | Fills the 7 entries at 0x800, loads them, can print them |
| `boot/gdt_flush.s` | **new** | `lgdt` + reloads CS/DS/ES/FS/GS/SS |
| `include/stack.h` | **new** | Stack tool + symbol table API |
| `kernel/stack.c` | **new** | The human-friendly stack printer |
| `kernel/symbols.c` | **new** | Turns an address into a function name |
| `include/multiboot.h` | **new** | The bits of the GRUB info struct and the ELF format we need |
| `include/shell.h`, `kernel/shell.c` | **new** | The bonus shell |
| `include/power.h`, `kernel/power.c` | **new** | reboot / halt / shutdown |
| `kernel/kernel.c` | modified | Calls `symbols_init` + `gdt_init`, adds TTY 3 (F4), routes keys to the shell |
| `kernel/printk.c` | modified | Supports widths and padding: `%08x`, `%-12s`, `%4u` |
| `kernel/vga.c`, `include/vga.h` | modified | 4 TTYs instead of 3, prompt `kfs2@42:~$`, backspace can go up a line |
| `include/keyboard.h` | modified | `SCANCODE_F4` |
| `include/io.h` | modified | `outw()` (16-bit port write, used by shutdown) |
| `boot/boot.s` | modified | Exports `stack_bottom`/`stack_top`, sets `EBP = 0` before `kernel_main` |
| `linker.ld` | modified | Defines `gdt_location`, `_text_start`, `_text_end`, `_kernel_start` |
| `Makefile` | modified | `-fno-omit-frame-pointer -fno-pie -MMD -MP`, new `run-monitor` rule |

---

## 1. The GDT (Global Descriptor Table)

### ELI5

Imagine the computer's memory is a huge building. The CPU is a guard at the door. Before anyone goes into a room, the guard checks a **list of rooms** taped to the wall:

- *"Room 1: Kernel Code - starts at floor 0, goes to the roof, only VIPs (ring 0), you can **run** things here."*
- *"Room 5: User Data - starts at floor 0, goes to the roof, everyone (ring 3) can **read and write** here."*

That list is the **GDT**. Each line is a **descriptor** (8 bytes). Each line has:

1. **Base** - where the room starts.
2. **Limit** - how big it is.
3. **Access** - what kind of room (code you can run, or data you can read/write) and who is allowed in (ring 0 = kernel, ring 3 = user programs).
4. **Flags** - "count the size in 4 KB pages" and "this is a 32-bit room".

When the kernel wants to use a room, it does not give the guard the room's address. It gives a **selector** - basically "line number × 8" - and the guard looks it up.

### What our table looks like

| # | Selector | Name | Base | Limit | Access | Meaning |
|---|---|---|---|---|---|---|
| 0 | `0x00` | Null | 0 | 0 | `0x00` | Mandatory empty line. The CPU refuses to use selector 0. |
| 1 | `0x08` | Kernel Code | 0 | 4 GB | `0x9A` | present, ring 0, code, executable + readable |
| 2 | `0x10` | Kernel Data | 0 | 4 GB | `0x92` | present, ring 0, data, readable + writable |
| 3 | `0x18` | Kernel Stack | 0 | 4 GB | `0x92` | same as data, used by **SS** |
| 4 | `0x23` | User Code | 0 | 4 GB | `0xFA` | like kernel code but ring 3 |
| 5 | `0x2B` | User Data | 0 | 4 GB | `0xF2` | like kernel data but ring 3 |
| 6 | `0x33` | User Stack | 0 | 4 GB | `0xF2` | like user data, for a future user SS |

All flags are `0xC` = "granularity 4 KB" + "32-bit". With limit `0xFFFFF` pages × 4 KB = 4 GB.

User selectors end in `3` (`0x23` and not `0x20`) because the 2 lowest bits of a selector are the **RPL** ("I am asking as ring 3").

The raw bytes written at `0x800` are (checked by a test on the host):

```
0x800: 00000000 00000000   Null
0x808: 0000FFFF 00CF9A00   Kernel Code
0x810: 0000FFFF 00CF9200   Kernel Data
0x818: 0000FFFF 00CF9200   Kernel Stack
0x820: 0000FFFF 00CFFA00   User Code
0x828: 0000FFFF 00CFF200   User Data
0x830: 0000FFFF 00CFF200   User Stack
```

### Why every segment covers all 4 GB ("flat model")

If every room covers the whole building, an address is just an address: no mental math with bases. This is what Linux and almost all modern kernels do. The GDT still matters because of the **privilege** and **type** bits (ring 0 vs ring 3, code vs data). Paging (KFS_3) will do the real memory protection later.

### Why the stack segments are "normal data" segments

The CPU also has an "expand-down" kind of data segment meant for stacks. With a flat model it would block the low addresses and break C code. In C, `&my_local_variable` is a stack address that later gets used through **DS**, not SS, so the stack has to live in the same flat space. So the stack entries are read/write data segments. What makes entry 3 the *kernel stack* segment is that **SS is loaded with it** (`SS = 0x18`). You can check that with the `regs` command.

### Why at 0x800, and how we write there

The subject requires it. Low memory looks like this:

```
0x00000 - 0x003FF  Real-mode interrupt table (BIOS)
0x00400 - 0x004FF  BIOS Data Area
0x00500 - 0x07BFF  free  <-- 0x800 is here, and our 56 bytes fit easily
0x07C00 - ...      (where the boot sector used to be, then more free memory)
```

`linker.ld` defines an absolute symbol: `gdt_location = 0x00000800;`. In C we write `extern gdt_entry_t gdt_location[];` and fill `gdt_location[0..6]`.

Why not just `(gdt_entry_t *)0x800`? Recent GCC versions think a tiny constant address is "probably a NULL pointer". With `-O2` they then emit `-Warray-bounds`, which `-Werror` turns into a build failure. A linker symbol hides the number from the compiler, and the address lives in a single place.

### `gdt_init()` step by step (`kernel/gdt.c`)

1. `gdt_set_entry()` is called 7 times. The descriptor format is old (from the 286), so base and limit are **cut into pieces** spread over the 8 bytes: `base_low`, `base_middle`, `base_high`, `limit_low`, and the high 4 bits of the limit share a byte with the flags. The function only does that cutting.
2. It builds a 6-byte `gdt_ptr_t { limit = 7*8-1 = 0x37, base = 0x800 }`.
3. It calls `gdt_flush(&gdtr)`.

### `gdt_flush` (`boot/gdt_flush.s`)

```nasm
lgdt [eax]          ; "Guard, here is the NEW list of rooms" -> GDTR register
mov ds/es/fs/gs, 0x10   ; data segments -> Kernel Data
mov ss, 0x18            ; stack segment -> Kernel Stack
jmp 0x08:.reload_cs     ; far jump: the only way to change CS -> Kernel Code
```

**ELI5:** `lgdt` changes the list on the wall, but every segment register still remembers the room it was given by GRUB (the CPU keeps a cached copy). So we "re-enter" each room with the new list. CS can't be changed with `mov`; a *far jump* changes CS and EIP at the same time.

This is done in assembly because C has no way to say "load SS" or "far jump".

### Order in `kernel_main`

```c
vga_init(); keyboard_init();
check multiboot magic;
symbols_init(mbi);   // copy GRUB's symbol table FIRST...
gdt_init();          // ...THEN write at 0x800
```

GRUB may have put its own data (the info struct, the symbol table) somewhere in low memory. We copy what we need before we write at 0x800, so we never read something we have just overwritten.

### `gdt_print()`

It asks the CPU itself where the table is (`sgdt`, the reverse of `lgdt`) and decodes each entry back into base / limit / access / DPL / type. So what you see is what the CPU really uses, not what we think we wrote. This is the `gdt` shell command and the top of the F4 screen.

---

## 2. The kernel stack printer

### ELI5: what is the stack?

A stack of plates. You can only **push** a plate on top or **pop** the top plate. Every time a function is called, the CPU and the compiler push a few "plates":

```
      high addresses  (stack_top: the stack starts here, empty)
   | arguments of the function |
   | return address            |  <- [EBP+4] "when done, go back HERE in the caller"
   | caller's EBP              |  <- [EBP]   "link to the previous plate group"
   | local variables           |
   | ...                       |  <- ESP     (the current top of the pile)
      low addresses   (stack_bottom: if ESP goes below, overflow!)
```

On x86 the pile grows **downwards**: pushing makes ESP *smaller*.

Our kernel stack is the 16 KB reserved in `boot/boot.s` (`stack_bottom` → `stack_top`). Those two labels are now `global` so C can see them.

### The EBP chain = the backtrace

Each function saves the previous EBP and then points EBP at that saved value. That makes a **linked list through the stack**: from the current EBP you can read `[EBP]` (previous frame) and `[EBP+4]` (where to return). Following it gives the list of functions currently running. That list is the **call trace**.

Two small changes make this reliable:
- `-fno-omit-frame-pointer` in the Makefile: forces GCC to always keep EBP as that chain, even if someone adds `-O2`.
- `xor ebp, ebp` in `boot/boot.s` before `call kernel_main`: the chain ends with a **0**, so the walker knows where to stop instead of following garbage left by GRUB.

`collect_frames()` walks the chain. For safety it also stops if an EBP leaves the stack area, is not 4-byte aligned, or does not go "up" (that would mean a corrupted stack).

### From addresses to names (`kernel/symbols.c`)

`0x00102330` means nothing to a human. `cmd_stack+0x7c` does.

`kernel.bin` is an ELF file and still contains its **symbol table** (`.symtab`: name, address, size of each function) and the names themselves (`.strtab`). These sections are not loaded with the code, but **GRUB loads them anyway** and tells us where they are in the multiboot info struct (field `elf_sec`, flag bit 5). `include/multiboot.h` describes only the fields we need.

`symbols_init()`:
1. finds the section of type `SHT_SYMTAB` and its string table (`sh_link`),
2. keeps the functions (`FUNC`) and local assembly labels that are inside `.text`,
3. **copies** them into a static array in our own `.bss` (up to 256 names of 31 chars), so we never depend on GRUB's memory again.

`symbol_lookup(addr)` returns the function with the biggest start address ≤ `addr`, and the offset inside it.

Small trick: a return address points to the instruction *after* the `call`. If the `call` is the last instruction of a function, the return address already belongs to the next function. So we look up `ret - 1` (always inside the `call`) and add 1 back to the offset. This is `caller_lookup()`.

If you boot with `make run-kernel` (QEMU `-kernel`, no GRUB), QEMU does not provide those sections. The tool then shows raw addresses and tells you to boot the ISO.

### What `print_kernel_stack()` shows

Three parts, with colors on screen. The example below shows the layout; the numbers are made up and will differ on your machine:

```
============================ KERNEL STACK ============================
 Segment : SS = 0x18 (Kernel Stack)                 <- proof SS uses our GDT entry 3
 Region  : 0x00106000 (bottom) -> 0x0010a000 (top) = 16384 bytes
 ESP     : 0x00109ec0   EBP : 0x00109ed8   (grows DOWN from top)
 Usage   : 320 / 16384 bytes (1%) [#-----------------------------]
 Call trace (newest first):
  #0  [0x00109ed8] print_kernel_stack     <- called by cmd_stack+0x7c
  #1  [0x00109ef8] cmd_stack              <- called by shell_execute+0xd1
  #2  [0x00109f48] shell_execute          <- called by shell_handle_char+0x5e
  #3  [0x00109f68] shell_handle_char      <- called by kernel_main+0x2a3
  #4  [0x00109ff8] kernel_main            <- called by _start+0x12
 Raw dump ESP -> top  (saved EBP, return address):
  0x00109ec0 +0x000: 00000005 00000000 0010a000 00106000  ................
  ...
  ... 240 more bytes up to stack_top (type 'stack all')
```

- **Summary**: which segment SS uses, where the stack is, ESP/EBP, how full it is (with a little bar).
- **Call trace**: read it as "`print_kernel_stack` is running, it was called by `cmd_stack`, which was called by `shell_execute`…" all the way back to `_start` in `boot.s`.
- **Raw dump**: the real bytes from ESP up to the top, 4 words per line, with an ASCII column. Words that are a **saved EBP** are shown in magenta and **return addresses** in red, so you can match the dump with the call trace.

The `frames[]` array is `static` (in `.bss`) on purpose: we don't want the tool to fill the stack it is trying to show.

`print_kernel_stack(n)` limits the dump to `n` lines (`0` = everything).

---

## 3. `printk` upgrade (`kernel/printk.c`)

To print aligned tables (GDT, stack dump), printk now understands:

| Format | Result |
|---|---|
| `%08x` with `0x1a2b` | `00001a2b` (pad with zeros to 8) |
| `%-13s` with `"Kernel Code"` | `Kernel Code  ` (left-aligned, 13 wide) |
| `%4u` with `42` | `  42` |
| `%05d` with `-42` | `-0042` |
| `%p` | always `0x` + 8 hex digits |

**ELI5:** between `%` and the letter, printk now reads optional **flags** (`-` = stick to the left, `0` = fill with zeros) and a **width** (minimum size). `print_number()` first writes the digits into a small buffer, so it knows how long the number is, then adds the right amount of padding before or after. These cases were tested on the host.

---

## 4. Bonus: the shell (`kernel/shell.c`)

Press **F2**.

### ELI5

The keyboard gives us one letter at a time. The shell is a notebook line:
1. Each letter you type is written in the notebook (`line[]`) and on the screen.
2. Backspace erases the last letter, but **never the prompt** (it only erases what you typed).
3. On **Enter**, the line is cut into words: `"stack   all"` → `argv = {"stack", "all"}`, `argc = 2`. The cutting is done in place by replacing spaces with `'\0'`.
4. The first word is looked up in a table of `{ name, usage, help, function }`. If found, the function is called with `argc/argv`, just like a C `main`.
5. Then a new prompt `kfs2@42:~$` is printed.

To add a command later, write a `cmd_xxx(int argc, char **argv)` function and add one line to the `commands[]` table. `help` is generated from that table.

### Commands

| Command | What it does |
|---|---|
| `help` | Lists the commands |
| `clear` | Clears the screen |
| `echo text…` | Prints the text |
| `stack` / `stack N` / `stack all` | Kernel stack (default 8 dump lines, or N, or everything) |
| `gdt` | The GDT as loaded in the CPU |
| `regs` | CS, DS, ES, FS, GS, SS with the GDT entry each one points to, plus ESP, EBP, EFLAGS, CR0 (PE = protected mode bit) and GDTR |
| `reboot` | Restarts the machine |
| `halt` | Stops the CPU (`cli; hlt` forever) |
| `shutdown` | Powers off QEMU / Bochs / VirtualBox |

### `kernel/power.c`

- **reboot**: asks the keyboard controller (8042, port `0x64`) to pulse the CPU reset line (command `0xFE`). That trick dates from the IBM PC/AT. If it doesn't work, we cause a **triple fault**: load an empty interrupt table and fire an interrupt. The CPU can't handle it, then can't handle that error either, then gives up and resets itself.
- **halt**: `cli` (ignore interrupts) + `hlt` (sleep until an interrupt). Since interrupts are ignored, it sleeps forever. The loop is there in case of an NMI.
- **shutdown**: there is no ACPI driver yet, so we write to the "magic" power-off ports of emulators (`0x604` QEMU, `0xB004` Bochs/old QEMU, `0x4004` VirtualBox). On real hardware nothing happens, so we fall back to halt.

---

## 5. Other small changes

- **4 screens** (`VGA_NUM_TTYS = 4`, `SCANCODE_F4 = 0x3E`):
  - F1 boot screen + "42",
  - F2 shell,
  - F3 colors,
  - F4 a snapshot of the GDT and the kernel stack taken at boot. That way the mandatory part can be shown even without the bonus shell.
- **Keyboard routing** in `kernel_main`: on the shell screen, letters go to `shell_handle_char()`. On the other screens they are simply echoed, as in KFS_1.
- **Backspace** in `vga_putchar` can now go back up to the end of the previous line, for shell commands longer than one line.
- **`linker.ld`**: `_text_start`/`_text_end` (so the symbol loader only keeps code symbols), `_kernel_start`, `gdt_location`, and `.text.*` is now collected too.
- **`boot.s` / `gdt_flush.s`**: `global _start:function` and `gdt_flush:function` mark them as functions in the symbol table (so they appear by name in the call trace). A `.note.GNU-stack` section removes the linker warning "missing .note.GNU-stack section implies executable stack".
- **Makefile**:
  - `-fno-omit-frame-pointer` (backtrace),
  - `-fno-pie` (plain fixed-address code, no GOT helper calls),
  - `-MMD -MP` + `-include $(OBJS:.o=.d)` (editing a `.h` now rebuilds the `.c` files that use it),
  - new rule `run-monitor`.

---

## 6. How to check everything during the evaluation

```sh
make re && make run          # boots the ISO with GRUB
```

1. **F1**: the boot screen shows `[OK] GDT chargee : 7 descripteurs a 0x00000800`.
2. **F4**: GDT table + kernel stack snapshot.
3. **F2**: type `help`, `gdt`, `regs` (look at `SS = 0x0018 -> GDT[3] Kernel Stack`), `stack`, `stack all`, then `reboot` / `halt` / `shutdown`.
4. Check it **from outside the kernel** with the QEMU monitor:

```sh
make run-monitor
(qemu) info registers     # line "GDT=     00000800 00000037"  -> base 0x800, 7 entries
                          # lines "CS =0008 ...", "SS =0018 ..."
(qemu) x /14wx 0x800      # the 7 raw descriptors shown in section 1
```

5. Size: `du -sh kfs.iso` → ~3.5 MB (< 10 MB).

> **Note:** this was compiled with `-Wall -Wextra -Werror` (also at `-O2`), and the GDT encoding, printk formatting, shell parsing and symbol lookup were unit-tested on a host machine. It could **not** be booted in QEMU on the machine where it was written, so run `make re && make run` once before the evaluation.
