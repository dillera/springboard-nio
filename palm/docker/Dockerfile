# Palm OS toolchain (palmdev: prc-tools-remix, PilRC, Palm OS SDKs) plus a
# 32-bit C++ compiler for Handspring's Palm-MakeROM.
FROM palmdev
RUN apt-get update && apt-get install -y --no-install-recommends g++-multilib python3 \
 && rm -rf /var/lib/apt/lists/*
