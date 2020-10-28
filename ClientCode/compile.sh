#!/bin/bash

g++ main.c $( sdl-config --cflags --libs ) -Wall -pedantic -lpthread -g
