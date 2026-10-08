# Fixed make build-tool interface.  Host gcc/g++ bootstrap cross ICK; only
# the cross ICK frontend compiles owned Ike C.  The NDK driver only links it.
REPOSITORY_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ICK_SOURCE ?= $(REPOSITORY_ROOT)/_ick
BUILD_ROOT ?= $(REPOSITORY_ROOT)/_/build/android-ick
MATERIALIZED := $(BUILD_ROOT)/source
ICK_REVISION := c5d28dde9cc333a562b907785d0370b725146cdf
GCC_REVISION := 6294f1d9e7536e5ffcde09d1528c918d63abfef5
BUILD_JOBS ?= 2
ANDROID_API ?= 21
NDK ?=
ABI ?= armeabi-v7a
OUTPUT ?= $(BUILD_ROOT)/$(ABI)/ike

ifeq ($(ABI),armeabi-v7a)
GNU_TARGET := arm-linux-gnueabi
BINUTILS_PREFIX := arm-linux-gnueabi
NDK_TRIPLE := arm-linux-androideabi
NDK_COMPILER := armv7a-linux-androideabi$(ANDROID_API)-clang
ICK_ABI_FLAGS := -march=armv7-a -mthumb -mfpu=neon -mfloat-abi=softfp
else ifeq ($(ABI),arm64-v8a)
GNU_TARGET := aarch64-linux-gnu
BINUTILS_PREFIX := aarch64-linux-gnu
NDK_TRIPLE := aarch64-linux-android
NDK_COMPILER := aarch64-linux-android$(ANDROID_API)-clang
ICK_ABI_FLAGS := -ffixed-x18
else
$(error unsupported Ike package ABI $(ABI))
endif

OBJECTS := $(BUILD_ROOT)/$(ABI)/objects
STAGE := $(BUILD_ROOT)/$(ABI)/stage
ICK := $(STAGE)/bin/$(GNU_TARGET)-gcc
ICK_OBJECT := $(BUILD_ROOT)/$(ABI)/ike.ick.o
NDK_TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64
SYSROOT := $(NDK_TOOLCHAIN)/sysroot
# Bionic's documented opt-out retains the real signed ioctl declaration.
# Its convenience overload requires Clang-only overloadable/enable_if syntax;
# ICK compiles the source and the NDK driver remains an object-only linker.
ICK_HEADER_FLAGS := -DBIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD

.PHONY: all
all: $(OUTPUT)

$(MATERIALIZED)/.materialized:
	test "$$(git -C "$(ICK_SOURCE)" rev-parse HEAD)" = "$(ICK_REVISION)"
	test "$$(git -C "$(ICK_SOURCE)/gcc" rev-parse HEAD)" = "$(GCC_REVISION)"
	mkdir -p "$(BUILD_ROOT)"
	sh "$(ICK_SOURCE)/ick/materialize.sh" "$(ICK_SOURCE)/gcc" "$(MATERIALIZED)"
	touch "$@"

$(OBJECTS)/Makefile: $(MATERIALIZED)/.materialized
	mkdir -p "$(OBJECTS)"
	cd "$(OBJECTS)" && "$(MATERIALIZED)/configure" --target="$(GNU_TARGET)" --prefix="$(STAGE)" --with-as=/usr/bin/$(BINUTILS_PREFIX)-as --with-ld=/usr/bin/$(BINUTILS_PREFIX)-ld --enable-languages=c --disable-bootstrap --disable-multilib --disable-nls --disable-shared --disable-threads --without-headers --disable-libatomic --disable-libgomp --disable-libquadmath --disable-libssp --disable-libvtv --without-isl --without-zstd

$(ICK): $(OBJECTS)/Makefile
	$(MAKE) -C "$(OBJECTS)" -j$(BUILD_JOBS) all-gcc
	$(MAKE) -C "$(OBJECTS)" install-gcc
	test "$$($(ICK) -dumpmachine)" = "$(GNU_TARGET)"

$(ICK_OBJECT): $(REPOSITORY_ROOT)/ike.c $(ICK)
	test -x "$(NDK_TOOLCHAIN)/bin/$(NDK_COMPILER)"
	$(ICK) $(ICK_ABI_FLAGS) $(ICK_HEADER_FLAGS) -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -fPIE -D__ANDROID__ -D__ANDROID_API__=$(ANDROID_API) --sysroot="$(SYSROOT)" -nostdinc -isystem "$$($(ICK) -print-file-name=include)" -isystem "$(SYSROOT)/usr/include" -isystem "$(SYSROOT)/usr/include/$(NDK_TRIPLE)" -c "$<" -o "$@"
	sha256sum "$(REPOSITORY_ROOT)/ike.c" "$@" > "$(BUILD_ROOT)/$(ABI)/source-and-object.sha256"

$(OUTPUT): $(ICK_OBJECT)
	mkdir -p "$(@D)"
	"$(NDK_TOOLCHAIN)/bin/$(NDK_COMPILER)" -fPIE -pie "$<" -o "$@"
