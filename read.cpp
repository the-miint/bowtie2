/*
 * Out-of-line Read constructors.
 *
 * These ctors are inline in read.h by default for a trivial
 * performance win (the body is just reset() or init()). However, on
 * macOS aarch64, clang's inliner is aggressive enough to collapse the
 * implicit SStringExpandable member ctors together with the reset()
 * body into a sparse set of byte stores that ELIDES the vtable
 * initialization — every embedded BTDnaString/BTString/TBuf ends up
 * with a NULL vtable pointer. This manifests as a SIGSEGV at
 * address 0x10 the first time any of those strings is assigned to
 * (virtual dispatch through the null vtable tries to load from
 * offset 0x10 of a null pointer).
 *
 * The failure reproduces reliably when bowtie2 is linked into a
 * Rust binary on macOS aarch64 via the `cc` crate. The same sources
 * built as a standalone C++ binary via CMake on the same platform do
 * not crash — so this is a codegen/context-specific pathology, not a
 * universal clang bug. Moving these ctor bodies out-of-line prevents
 * the inliner from seeing them during the array-new codegen and
 * restores correct vtable initialization.
 */

#include "read.h"

Read::Read() {
	reset();
}

Read::Read(const char *nm, const char *seq, const char *ql) {
	init(nm, seq, ql);
}
