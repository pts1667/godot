def can_build(env, platform):
    env.module_add_dependencies("sourcepp", ["halfedge"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "BSPShape3D",
        "SourceAnimPlayer",
        "SourcePPBrushArea3D",
        "SourcePPBrushBody3D",
        "SourcePPBrushEntity3D",
        "SourcePPBSP",
        "SourcePPFuncBrush3D",
        "SourcePPFuncDoor3D",
        "SourcePPFuncIllusionary3D",
        "SourcePPLadder3D",
        "SourcePPLadderDismount3D",
        "SourcePPMDL",
        "SourcePPResolver",
        "SourcePPTriggerHurt3D",
        "SourcePPTriggerMultiple3D",
        "SourcePPTriggerOnce3D",
        "SourcePPVMT",
        "SourcePPVPK",
        "SourcePPVTF",
    ]


def get_doc_path():
    return "doc_classes"
