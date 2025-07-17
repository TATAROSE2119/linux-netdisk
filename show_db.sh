#!/bin/bash

echo "👥 Users table content:"
echo ".headers on" | sqlite3 netdisk.db "SELECT * FROM users;"
