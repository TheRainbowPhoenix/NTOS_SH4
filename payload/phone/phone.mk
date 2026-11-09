# Lightweight WinCE-first phone shell; all six DLLs prefer 0x10000000.
.PHONY: phone phone-check phone-preview
phone: $(COREDLL_DLL) $(APP_HH3)
	$(PYTHON) payload/phone/build_phone.py --out $(OUTDIR) --obj $(BUILDDIR)/phone
phone-check: phone
	$(PYTHON) payload/phone/check_phone.py --out $(OUTDIR)
phone-preview:
	$(PYTHON) payload/phone/preview_phone.py
ntx: phone
