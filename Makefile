PLUGIN_DIR = $(HOME)/Library/Application Support/obs-studio/plugins

configure:
	cmake --preset macos

index:
	cmake --preset index
	ln -sfn build_index/compile_commands.json compile_commands.json

build:
	cmake --build --preset macos

clean:
	rm -rf build_macos

format:
	./build-aux/run-gersemi
	./build-aux/run-clang-format

link:
	mkdir -p "$(PLUGIN_DIR)"
	ln -sfn "$(PWD)/build_macos/RelWithDebInfo/font-meets-clock.plugin" \
					"$(PLUGIN_DIR)/font-meets-clock.plugin"
