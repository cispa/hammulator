#!/usr/bin/env bash
cd gem5 || exit
git am ../gem5-patches/*
cd ext/dramsim3/DRAMsim3 || exit
git am ../../../../DRAMsim3-patches/*
