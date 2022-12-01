/*
 * Sam Pazdan
 * PDP-11 Simulator
 *
 * This is a PDP-11 instruction set simulator. It supports all addressing modes and the subset of instructions
 * that can be viewed in the #defines down below.
 *
 * After compiling, the simulator can be used with this command: ./[execname] [-t | -v] < [codefile]
 *
 * The -t tracing option will enable simple instruction tracing. This will show instructions/addresses as they are executed.
 * It also shows the source and destination addressing modes (when applicable).
 *
 * The -v verbose option will enable verbose tracing. This option will, in addition to printing instruction tracing,
 * print the register values, source and destination values, and nzvc bits after each command. It will also print the
 * first 20 words of memory after execution.
 *
 * Execution statistics can be viewed at the end of program execution.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define MEM_SIZE 32*1024

//instructions:
#define MOV 1
#define CMP 2
#define ADD 6
#define SUB 14

#define SOB 63
#define BR 4
#define BNE 1
#define BEQ 12

#define ASR 50
#define ASL 51

//addressing modes:
#define REG 0
#define REG_DEF 1
#define AUTO_INC 2
#define AUTO_INC_DEF 3
#define AUTO_DEC 4
#define AUTO_DEC_DEF 5
#define INDEX 6
#define INDEX_DEF 7

typedef struct ap{
    int mode;
    int reg;
    int addr; //only used for address indirection
    int value;
} addr_phrase;

int reg[8] = {0};
int mem[MEM_SIZE] = {0};

//processor
int halt = 0, cond_codes = 0;
unsigned cc_n, cc_c, cc_z, cc_v;
int result = 0, ind = 0;

//decoding
unsigned opcode, instruction;
addr_phrase src, dst;

//tracking
int instr_exec = 0,
    instr_fetch = 0,
    words_read = 0,
    words_written = 0,
    br_exec = 0,
    br_taken = 0,
    instr_trace = 0,
    verbose = 0;

void load_program(){
    int k;
    int i = 0;

    if(verbose) {
        printf("reading words in octal from stdin:\n");
    }
    while (scanf("%o", &k) == 1) {
        mem[i] = k;
        if(verbose) {
            printf("  0%06o\n", k);
        }
        i++;
    }
}

void print_regs(){
    if(verbose) {
        printf("  R0:0%06o  R2:0%06o  R4:0%06o  R6:0%06o\n", reg[0], reg[2], reg[4], reg[6]);
        printf("  R1:0%06o  R3:0%06o  R5:0%06o  R7:0%06o\n", reg[1], reg[3], reg[5], reg[7]);
    }
}

void printsrcval(){
    if(verbose) {
        printf("  src.value = 0%06o\n", src.value);
    }
}

void printdstval(){
    if(verbose) {
        printf("  dst.value = 0%06o\n", dst.value);
    }
}

void printresult() {
    if (verbose) {
        printf("  result    = 0%06o\n", result);
    }
}

void printbits(){
    if(verbose) {
        printf("  nzvc bits = 4'b%u%u%u%u\n", cc_n, cc_z, cc_v, cc_c);
    }
}

void get_operand(addr_phrase *p){
    switch(p->mode){
        case REG:
            p->value = reg[p->reg];
            p->addr = 0;
            break;

        case REG_DEF:
            p->addr = reg[p->reg];
            p->value = mem[ p->addr >> 1 ];
            words_read++;
            break;

        case AUTO_INC:
            //increment fetched instructions if reading from next mem slot
            if(p->reg == 7){
                instr_fetch++;
            }
            p->addr = reg[ p->reg ];
            assert( p->addr < 0200000 );
            p->value = mem[ p->addr >> 1 ];
            assert( p->value < 0200000 );
            reg[ p->reg ] = ( reg[ p->reg ] + 2 ) & 0177777;
            break;

        case AUTO_INC_DEF:
            words_read++;
            p->addr = reg[p->reg];
            p->addr = mem[p->addr >> 1];
            p->value = mem[p->addr >> 1];

            reg[p->reg] += 2;
            break;

        case AUTO_DEC:
            words_read++;
            reg[ p->reg ] = ( reg[ p->reg ] - 2 ) & 0177777;
            p->addr = reg[p->reg];
            p->value = mem[p->addr >> 1];
            break;

        case AUTO_DEC_DEF:
            words_read++;
            reg[p->reg] -= 2;
            p->addr = reg[p->reg];
            p->addr = mem[p->addr >> 1];
            p->value = mem[p->addr >> 1];
            break;

        case INDEX:
            instr_fetch++;
            words_read+=3;
            ind = mem[reg[7] >> 1];
            p->addr = (reg[p->reg] + ind) & 0177777;
            reg[7] = ( reg[7] + 2 ) & 0177777;
            p->value = mem[p->addr >> 1];
            break;

        case INDEX_DEF:
            instr_fetch++;
            words_read+=3;
            ind = mem[reg[7] >> 1];
            p->addr = (reg[p->reg] + ind) & 0177777;
            reg[7] = ( reg[7] + 2 ) & 0177777;
            p->addr = mem[p->addr >> 1];
            p->value = mem[p->addr >> 1];
            break;
    }
}

int main(int argc, char **argv) {

    if(argc > 1){
        if(strcmp(argv[1], "-t") == 0){
            instr_trace = 1;
        } else if (strcmp(argv[1], "-v") == 0){
            instr_trace = 1;
            verbose = 1;
        }
    }

    load_program();

    if(instr_trace) {
        printf("\ninstruction trace:\n");
    }

    while(!halt){

        if(instr_trace) {
            printf("at 0%04o, ", reg[7]);
        }

        instruction = mem[reg[7] >> 1];
        instr_exec++;
        instr_fetch++;
        reg[7] = ( reg[7] + 2 ) & 0177777;

        if(instruction == 0){
            if(instr_trace) {
                printf("halt instruction\n");
            }
            print_regs();
            halt = 1;
            break;
        }

        src.mode = (instruction >> 9) & 07;
        src.reg = (instruction >> 6) & 07;
        dst.mode = (instruction >> 3) & 07;
        dst.reg = instruction & 07;

        //decode the opcode and execute
        opcode = instruction >> 12;
        switch(opcode){
            case MOV:
                get_operand(&src);
                get_operand(&dst);

                if(instr_trace) {
                    printf("mov instruction sm %d, sr %d dm %d dr %d\n", src.mode, src.reg, dst.mode, dst.reg);
                }

                printsrcval();

                cc_n = (src.value & 0100000) ? 1 : 0;
                cc_z = src.value == 0 ? 1 : 0;
                cc_v = 0;

                printbits();

                if(dst.mode == 2){
                    mem[dst.addr >> 1] = src.value;
                    if(verbose) {
                        printf("  value 0%06o is written to 0%06o\n", src.value, dst.addr);
                    }
                    words_written++;
                } else {
                    reg[dst.reg] = src.value;
                }

                goto got_opcode;

            case CMP:
                get_operand(&src);
                get_operand(&dst);
                if(instr_trace) {
                    printf("cmp instruction sm %d, sr %d dm %d dr %d\n", src.mode, src.reg, dst.mode, dst.reg);
                }
                printsrcval();
                printdstval();
                result = src.value - dst.value;

                cc_c = (result & 0200000) >> 16;
                result = result & 0177777;
                printresult();

                cc_n = 0; if(result & 0100000) cc_n = 1;
                cc_z = 0; if(result == 0) cc_z = 1;

                cc_v = 0;
                if( ( (src.value & 0100000) != (dst.value & 0100000) ) &&
                    ( (src.value & 0100000) == (result & 0100000) ) ){
                    cc_v = 1;
                }

                printbits();

                goto got_opcode;

            case ADD:
                get_operand(&src);
                get_operand(&dst);

                if(instr_trace) {
                    printf("add instruction sm %d, sr %d dm %d dr %d\n", src.mode, src.reg, dst.mode, dst.reg);
                }

                printsrcval();
                printdstval();

                result = src.value + dst.value;

                result = result & 0177777;

                cc_v = 0; if((src.value & 0100000) == (dst.value & 0100000) && (src.value & 0100000) != (result & 0100000)) cc_v = 1;
                cc_c = 0; if(result < (src.value + dst.value)) cc_c = 1;
                cc_n = 0; if(result & 0100000) cc_n = 1;
                cc_z = 0; if(result == 0) cc_z = 1;

                printresult();
                printbits();
                reg[dst.reg] = result;

                goto got_opcode;

            case SUB:

                get_operand(&src);
                get_operand(&dst);

                if(instr_trace) {
                    printf("sub instruction sm %d, sr %d dm %d dr %d\n", src.mode, src.reg, dst.mode, dst.reg);
                }

                printsrcval();
                printdstval();
                result = dst.value - src.value;

                cc_c = (result & 0200000) >> 16;
                result = result & 0177777;
                cc_n = 0; if(result & 0100000) cc_n = 1;
                cc_z = 0; if(result == 0 ) cc_z = 1;
                cc_v = 0;
                if( ( (src.value & 0100000) != (dst.value & 0100000) ) &&
                    ( (src.value & 0100000) == (result & 0100000) ) ){
                    cc_v = 1;
                }

                printresult();
                printbits();
                reg[dst.reg] = result;
                goto got_opcode;
        }

        int offset;
        int new_addr;

        opcode = instruction >> 6;
        switch(opcode){
            case BR:
                br_exec++;
                br_taken++;

                offset = instruction & 0377;
                offset = offset << 24;
                offset = offset >> 24;
                new_addr = ( reg[7] + (offset << 1)) & 0177777;
                reg[7] = new_addr;

                if(instr_trace) {
                    printf("br instruction with offset %04o\n", offset);
                }

                goto got_opcode;

            case BEQ:
                br_exec++;

                offset = instruction & 0377;

                new_addr = ( reg[7] + (offset << 1));

                if(cc_z) {
                    reg[7] = new_addr;
                    br_taken++;
                }

                if(instr_trace) {
                    printf("beq instruction with offset %04o\n", offset);
                }

                goto got_opcode;

            case ASL:
                get_operand(&dst);

                if(instr_trace) {
                    printf("asl instruction dm %d dr %d\n", dst.mode, dst.reg);
                }

                printdstval();
                result = (uint16_t) (reg[dst.reg] << 1);

                printresult();

                cc_n = 0; if(result & 0100000) cc_n = 1;
                cc_z = 0; if(result == 0) cc_z = 1;
                if(dst.value & 0100000){
                    cc_c = 1;
                } else {
                    cc_c = 0;
                }
                cc_v = cc_c^cc_n;

                printbits();
                reg[dst.reg] = result;
                goto got_opcode;

            case ASR:
                get_operand(&dst);

                if(instr_trace) {
                    printf("asr instruction dm %d dr %d\n", dst.mode, dst.reg);
                }

                printdstval();
                result = reg[dst.reg];
                result = result << 16;
                result = result >> 16;

                result = (uint16_t) (result >> 1);

                cc_n = 0; if(result & 0100000) cc_n = 1;
                cc_z = 0; if(result == 0) cc_z = 1;
                if(dst.value & 0000001){
                    cc_c = 1;
                } else {
                    cc_c = 0;
                }
                cc_v = cc_c^cc_n;

                printresult();
                printbits();
                reg[dst.reg] = result;

                goto got_opcode;
        }

        opcode = instruction >> 9;
        switch(opcode){
            case SOB:
                br_exec++;
                src.mode = 0;
                get_operand(&src);

                offset = instruction & 077;
                offset = offset << 24;
                offset = offset >> 24;

                reg[src.reg]--;

                if(reg[src.reg] != 0) {
                    br_taken++;
                    new_addr = (reg[7] - (offset << 1)) & 0177777;
                    reg[7] = new_addr;
                }

                if(instr_trace) {
                    printf("sob instruction reg %d with offset %03o\n", src.reg, offset);
                }

                goto got_opcode;

            case BNE:
                br_exec++;
                offset = instruction & 0377;

                if(instr_trace) {
                    printf("bne instruction with offset %04o\n", offset);
                }

                offset = offset << 24;
                offset = offset >> 24;
                new_addr = ( reg[7] + (offset << 1));

                if(!cc_z) {
                    br_taken++;
                    reg[7] = new_addr;
                }

                goto got_opcode;
        }

        printf("\nBAD INSTRUCTION AT PC = %06o\n", reg[7] - 2);
        exit(-1);

        got_opcode:

        print_regs();
    }

    if(verbose || instr_trace) printf("\n");
    printf("execution statistics (in decimal):\n");
    printf("  instructions executed     = %d\n", instr_exec);
    printf("  instruction words fetched = %d\n", instr_fetch);
    printf("  data words read           = %d\n", words_read);
    printf("  data words written        = %d\n", words_written);
    printf("  branches executed         = %d\n", br_exec);
    printf("  branches taken            = %d", br_taken);
    if(br_exec != 0){
        double perc_taken = ((double) br_taken) / ((double) br_exec) * 100;
        printf(" (%.1f%%)", perc_taken);
    }

    if(verbose) {
        printf("\n\nfirst 20 words of memory after execution halts:\n");
        for (int i = 0; i < 20; i++) {
            printf("  0%04o: %06o", i * 2, mem[i]);
            if(i != 19){
                printf("\n");
            }
        }
    }
    return 0;
}
