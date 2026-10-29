SRCS := $(shell find src -name "*.cpp")

.PHONY: build
build: configure
	@cmake --build --preset clang

.PHONY: configure
configure:
	@cmake --preset clang

.PHONY: tidy
tidy: configure
	@clang-tidy --quiet -p build $(SRCS)

.PHONY: tidy-fix
tidy-fix: configure
	@clang-tidy --quiet -p build --fix $(SRCS)

.PHONY: format
format:
	@clang-format -i $(SRCS)
