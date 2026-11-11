# Checker self test, no MP_Pipeline involved
Import(ScriptDir() + "_config.avsi")
Import(ScriptDir() + "props_common.avsi")
PropsSource()
PropsVerify("control")
