#!/bin/bash
#--cache-objects=false \
#--debug-fuse \
rclone -vv \
--allow-other \
--allow-root \
--transfers 32 \
--attr-timeout 10m \
--dir-cache-time 24h \
--poll-interval 0 \
--buffer-size 1M \
--vfs-cache-mode writes \
--vfs-cache-max-age 2m \
--vfs-cache-max-size 256G \
--vfs-cache-poll-interval 1m \
--vfs-write-back 2m \
--vfs-read-chunk-size 256K \
--vfs-read-chunk-size-limit 128M \
mount jotta:treefiles/local_test ./mount
