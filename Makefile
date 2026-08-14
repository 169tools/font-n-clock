PLUGIN_DIR = $(HOME)/Library/Application Support/obs-studio/plugins

configure:
	cmake --preset macos

build:
	cmake --build --preset macos

clean:
	rm -rf build_macos

link:
	mkdir -p "$(PLUGIN_DIR)"
	ln -sfn "$(PWD)/build_macos/rundir/RelWithDebInfo/font-meets-clock.plugin" \
					"$(PLUGIN_DIR)/font-meets-clock.plugin"
