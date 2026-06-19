import os

env = DefaultEnvironment()

# 仅在 esptool 协议下注入 --configfile（espota OTA 不需要）
upload_protocol = env.subst("$UPLOAD_PROTOCOL")
if upload_protocol == "esptool":
    print("-> Enabling custom esptool config...")
    env.Append(UPLOADERFLAGS=["--configfile", "esptool.cfg"])
else:
    print(f"-> Skipping esptool config (protocol={upload_protocol})")