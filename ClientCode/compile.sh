#!/bin/bash

gcc main.c $( sdl-config --cflags --libs ) -Wall -pedantic -lpthread -g
