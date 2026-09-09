PrjDir = $(shell pwd)
VersionFile = $(PrjDir)/VERSION
WixProject = $(PrjDir)/sshfs-win.wixproj
MyProductName = "SSHFS-Win"
MyCompanyName = "OzoneAsai"
MyDescription = "Modernized SSHFS for Windows"
MyVersion = $(strip $(shell cat "$(VersionFile)"))
MyProductVersion = "$(MyVersion) Preview"
MyProductStage = "Beta"

HostArch := $(shell uname -m)
ifeq ($(HostArch),x86_64)
	MyArch = x64
else ifneq ($(filter i686 i586 i486 i386,$(HostArch)),)
	MyArch = x86
else
	$(error Unsupported build host architecture '$(HostArch)'; supported Cygwin targets are x86_64 and x86)
endif

SigningIssuer ?= "DigiCert"
SigningSubject ?= $(MyCompanyName)
SigningCrossCert ?= "DigiCert High Assurance EV Root CA.crt"

BldDir	= .build/$(MyArch)
DistDir = $(BldDir)/dist
SrcDir	= $(BldDir)/src
RootDir	= $(BldDir)/root
WixDir	= $(BldDir)/wix
Status	= $(BldDir)/status
VendorCore ?= 1
VendorSSH ?= 1
VendorDir = $(BldDir)/vendor
VendorRuntime = $(VendorDir)/runtime
AllowUnsigned ?= 0

SSHFSCommit = 24448e2493533ead984d6ca322c583e1a26cc613
SSHFSMainBlob = 3bd4ec57b3bf7c5eb8aff68dafa6a84264186269
SSHFSMainTree = 35655f60d37403663238a73b4204cfd64fdca73c
SSHFS_PATCH_SERIES = $(PrjDir)/patches/SERIES
SSHFS_PATCHES = $(wildcard $(PrjDir)/patches/*.patch)
BinExtra= #bash ls mount

goal: $(Status) $(Status)/done

$(Status)/depcheck: | $(Status)
	tools/check-dependencies.sh --bootstrap
	touch $(Status)/depcheck

$(Status):
	mkdir -p $(Status)

$(Status)/done: $(Status)/dist
	touch $(Status)/done

$(Status)/dist: $(Status)/wix
	mkdir -p $(DistDir)
	@set -e; \
	src='$(WixDir)/sshfs-win-$(MyVersion)-$(MyArch).msi'; \
	dst='$(DistDir)/sshfs-win-$(MyVersion)-$(MyArch).msi'; \
	tmp="$$dst.signing"; \
	rm -f "$$tmp" "$$dst"; \
	cp "$$src" "$$tmp"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	msi=$$(cygpath -aw "$$tmp"); \
	if sed 's/\r$$//' tools/signtool | bash -s -- sign \
		/ac tools/$(SigningCrossCert) \
		/i $(SigningIssuer) \
		/n $(SigningSubject) \
		/d $(MyDescription) \
		/fd sha256 \
		/tr https://timestamp.digicert.com \
		/td sha256 \
		"$$msi"; then \
		echo "signed: $$msi"; \
	elif test "$(AllowUnsigned)" = "1"; then \
		echo "WARNING: signing failed; publishing unsigned local MSI because AllowUnsigned=1" >&2; \
	else \
		echo "ERROR: signing failed; no distribution MSI was published." >&2; \
		echo "Set SigningSubject/SigningIssuer for the fork's signing certificate, or use AllowUnsigned=1 for deliberate local-only builds." >&2; \
		exit 1; \
	fi; \
	mv -f "$$tmp" "$$dst"; \
	trap - EXIT HUP INT TERM
	touch $(Status)/dist

$(Status)/wix: $(Status)/sshfs-win sshfs-win.wxs sshfs-win.wixproj $(VersionFile)
	mkdir -p $(WixDir)
	dotnet build "$(shell cygpath -aw $(WixProject))" \
		--configuration Release \
		--property:InstallerPlatform=$(MyArch) \
		--property:OutputName=sshfs-win-$(MyVersion)-$(MyArch) \
		--property:OutputPath="$(shell cygpath -aw $(WixDir))" \
		--property:IntermediateOutputPath="$(shell cygpath -aw $(WixDir)/obj)" \
		--property:MyProductName=$(MyProductName) \
		--property:MyCompanyName=$(MyCompanyName) \
		--property:MyDescription=$(MyDescription) \
		--property:MyProductVersion=$(MyProductVersion) \
		--property:MyProductStage=$(MyProductStage) \
		--property:MyVersion=$(MyVersion) \
		--property:RootDir="$(shell cygpath -aw $(RootDir))"
	test -f "$(WixDir)/sshfs-win-$(MyVersion)-$(MyArch).msi"
	touch $(Status)/wix

$(Status)/sshfs-win: $(Status)/root sshfs-win.c portable-format.h
	gcc -o $(RootDir)/bin/sshfs-win sshfs-win.c
	strip $(RootDir)/bin/sshfs-win
	wrapper="$(RootDir)/bin/sshfs-win"; \
	if test -f "$$wrapper.exe"; then wrapper="$$wrapper.exe"; fi; \
	base=$$(basename "$$wrapper"); \
	if ! awk -F '\t' -v b="$$base" '$$1 == b {found=1} END {exit !found}' $(RootDir)/etc/runtime-origins.tsv; then \
		printf '%s\t%s\t%s\n' "$$base" local:sshfs-win "$$wrapper" >> $(RootDir)/etc/runtime-origins.tsv; \
	fi
	tools/write-dependency-manifest.sh $(RootDir)
	tools/audit-runtime.sh $(RootDir)
	touch $(Status)/sshfs-win

$(Status)/vendor-core: $(Status)/depcheck | $(Status)
ifeq ($(VendorCore),1)
	tools/build-vendor-core.sh $(VendorDir)
	touch $(Status)/vendor-core
else
	@echo "VendorCore disabled: release provenance checks will reject unpinned GLib/PCRE2"
	touch $(Status)/vendor-core
endif

$(Status)/vendor-ssh: $(Status)/vendor-core | $(Status)
ifeq ($(VendorSSH),1)
	PKG_CONFIG_PATH="$(abspath $(VendorDir))/prefix/lib/pkgconfig" tools/check-dependencies.sh --vendor-ssh
	tools/build-vendor-ssh.sh $(VendorDir)
	touch $(Status)/vendor-ssh
else
	@echo "VendorSSH disabled: runtime must provide an explicitly configured ssh client"
	touch $(Status)/vendor-ssh
endif

$(Status)/root: $(Status)/make $(Status)/vendor-ssh
	mkdir -p $(RootDir)/{bin,etc,dev/{mqueue,shm}}
	cp -R $(PrjDir)/etc/. $(RootDir)/etc/
ifeq ($(VendorSSH),1)
	tools/copy-runtime-closure.sh $(RootDir) $(VendorDir) \
		$(SrcDir)/sshfs/build/sshfs.exe $(VendorRuntime)/bin/ssh.exe
else
	tools/copy-runtime-closure.sh $(RootDir) $(VendorDir) \
		$(SrcDir)/sshfs/build/sshfs
endif
	for f in $(BinExtra); do tools/copy-runtime-closure.sh $(RootDir) $(VendorDir) /usr/bin/$$f; done
	for f in pcre2.commit glib.commit pcre2-version.txt glib-version.txt \
		openssl.commit openssh.commit openssl-version.txt openssh-version.txt; do \
		if test -f $(VendorRuntime)/$$f; then cp -f $(VendorRuntime)/$$f $(RootDir)/etc/vendor-$$f; fi; \
	done
	tools/write-dependency-manifest.sh $(RootDir)
	tools/audit-runtime.sh $(RootDir)
	touch $(Status)/root

$(Status)/make: $(Status)/config
	cd $(SrcDir)/sshfs/build && ninja
	touch $(Status)/make

$(Status)/config: $(Status)/patch $(Status)/depcheck $(Status)/vendor-core
	mkdir -p $(SrcDir)/sshfs/build
ifeq ($(VendorCore),1)
	cd $(SrcDir)/sshfs/build && \
		PKG_CONFIG_PATH="$(abspath $(VendorDir))/prefix/lib/pkgconfig" \
		PATH="$(abspath $(VendorDir))/prefix/bin:$$PATH" \
		meson setup .. --wrap-mode=nodownload
else
	cd $(SrcDir)/sshfs/build && meson setup ..
endif
	touch $(Status)/config

$(Status)/patch: $(Status)/clone $(SSHFS_PATCH_SERIES) $(SSHFS_PATCHES)
	set -e; cd $(SrcDir)/sshfs; \
	while IFS= read -r name || test -n "$$name"; do \
		case "$$name" in ''|\#*) continue ;; esac; \
		f="$(PrjDir)/patches/$$name"; \
		test -f "$$f" || { echo "missing patch listed in SERIES: $$name" >&2; exit 1; }; \
		echo "applying $$f"; \
		tmp=$$(mktemp); \
		sed 's/\r$$//' <"$$f" >"$$tmp"; \
		git apply --check --whitespace=error-all "$$tmp"; \
		git apply --whitespace=error-all "$$tmp"; \
		rm -f "$$tmp"; \
	done < "$(SSHFS_PATCH_SERIES)"; \
	git hash-object sshfs.c > $(abspath $(Status))/patched-sshfs-c.blob
	touch $(Status)/patch

$(Status)/clone:
	mkdir -p $(SrcDir)
	rm -rf "$(SrcDir)/sshfs"
	cp -a "$(PrjDir)/sshfs" "$(SrcDir)/sshfs"
	tools/verify-sshfs-source.sh --source-only "$(SrcDir)/sshfs"
	touch $(Status)/clone

clean:
	rm -rf -- "$(PrjDir)/.build"
