# Tropic Trail: the 3D tutorial series (docs/video/PIANO.md). Skeleton only:
# every step calls video/3d/run.py, which prints the plan until record.py of the
# episode exists. The key overlay is specified in claude/skills/bm-video-series/overlay.md.
V3D_PY ?= python3
V3D_NUMS := 01 02 03 04 05 06 07 08

define V3D_RULE
video-3d-$(1):
	$$(V3D_PY) video/3d/run.py $(1)
endef
$(foreach n,$(V3D_NUMS),$(eval $(call V3D_RULE,$(n))))

video-3d: $(addprefix video-3d-,$(V3D_NUMS))

video-3d-covers:
	@for c in docs/video/*/cover.json; do \
	  d=$$(dirname $$c); \
	  $(V3D_PY) video/lib/cover.py $$c -o $$d/cover.png || exit 1; \
	done

video-3d-check:
	$(V3D_PY) video/3d/check_series.py

.PHONY: video-3d video-3d-covers video-3d-check $(addprefix video-3d-,$(V3D_NUMS))
