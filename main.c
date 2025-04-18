/*
 * main.c
 * 
 * Copyright (C) 2025.4.18 TOP WAYE topwaye@hotmail.com
 * 
 * main framework of an operating system
 */

#include <stdio.h>

void on_stop_device ( int n ) /* interrupt */
{
    printf ( "device %d done.\n", n );
}

void start_device ( int n )
{
    printf ( "starting device %d...\n", n );
}

void task_1 ( void )
{
    start_device ( 1 );
}

void task_2 ( void )
{
    start_device ( 2 );
}

int main ( void )
{
    task_1 ( );
    task_2 ( );

    return 0;
}