#!/usr/bin/env sh

if [ -z "$TMUX" ]; then
    tmux \
        new-session  "$@" \; \
        split-window 'sleep 5 && m5term localhost 3456' \;
else
    # already in tmux session
    tmux split-window 'sleep 5 && m5term localhost 3456' \;
    "$@"
fi
