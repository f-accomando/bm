"""bm_core: headless read / write / check of bm cartridges (.bm,
.bme projects, BMRES files) and of their MESH (8) and ANIM (9) sections.

Python >= 3.8, standard library only, no network. See README.md.
"""
from .errors import ValidationError
from .container import (Cart, Header, Section, build, check_path, edition_of, edition_of_cart, new_cart, parse,
                        read_file, validate, write_file)
from .mesh import Face, Mesh, Model
from .anim import Anim, Bone, Clip, Key, Pose, Rig
from . import anim, animgen, container, export, imageio, mesh, preview, sections, shapes, skelgen, skin

__all__ = ["ValidationError", "Cart", "Header", "Section", "build", "check_path", "edition_of",
           "edition_of_cart", "new_cart", "parse", "read_file", "validate", "write_file", "Face", "Mesh",
           "Model", "Anim", "Bone", "Clip", "Key", "Pose", "Rig", "anim", "animgen", "container", "export", "imageio", "mesh",
           "preview", "sections", "shapes", "skelgen", "skin"]
