def can_build(env, platform):
    env.module_add_dependencies("sourcepp", ["halfedge"])
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "SourceAnimPlayer",
        "SourcePPBSP",
        "SourcePPMDL",
        "SourcePPResolver",
        "SourcePPVMT",
        "SourcePPVPK",
        "SourcePPVTF",
    ]


def get_doc_path():
    return "doc_classes"