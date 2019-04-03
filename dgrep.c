/*
 * Update: Everything below this comment block is original code I wrote in '93
 * as an exercise to myself in RE's.  Clearly dated with an ASCII view of the
 * world and "cutting edge" use of RE special escapes (\d, \w, etc).  Too funny.
 *
 * Note: One modification was required in error() varargs -> stdarg
 *
 * Dave -- Wed Feb 13 13:30:02 MST 2013
 */


/*  Copyright (c) 1992 Dave Farnham                     */
/*    All Rights Reserved                               */
/*                                                      */
/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF      */
/*                 Dave Farnham                         */
/*                                                      */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */
/*                                                      */
/* dgrep.c  -- replacement for "egrep", with extensions */

static char sccsid[] = "@(#)dgrep.c 1.2 93/03/20 20:11:44 Dave Farnham";

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <memory.h>
#include <errno.h>
#include <stdarg.h>

/*
 * Initial allocation guess.  Memory will
 *  be realloc()'d as needed.
 */
#define NUM_NEXT_PTRS     2
#define NUM_RE_STATES    10
#define BUFSZ          1024


/*
 * For now, only allow up to 9 backreferences,
 *  we deviate from "perl" here.  Perhaps later
 *  I'll allow \10 etc. ??
 */
#define NUM_CAPTURES      9

/*
 * Make sure these are out of the ASCII range
 */
#define ACCEPT_STATE    257
#define BEGIN_LINE      258
#define END_LINE        259
#define SET             260
#define NEGSET          261
#define END_SET         262
#define START_STATE     263
#define ANY_CHAR        264
#define ANYTHING        265
#define LITERAL_STAR    266
#define START_CAPTURE   267
#define END_CAPTURE     268
#define BACKREFERENCE   269
#define MIN_MAX         270



/*
 * Functions in this module
 */
void error(char *, ...);
void insert_sort();
void dup_capture();
int get_start_index();
void add_next_ptr();
void get_more_next_ptrs();
void get_more_re_states();
void init_re();
int do_min_max();
int do_escapes();
int make_set();
int compile_pattern();
int match();
int do_re();
void show_re();
static char *my_strpbrk();
static char *my_strstr();
int dgrepit();
int main();


/*
 * Global Data
 */
int Cap_start[NUM_CAPTURES], Cap_end[NUM_CAPTURES];
int Textlen;
char Text[BUFSZ];
int num_re_states = NUM_RE_STATES;


/*
 * Every "state" carries one of these around
 */
struct _RE {
    int type;
    int special;
    int backind;
    int capind;
    int num;
    int max_next;
    char *set;
    int *next;
} *RE;




/*
 * Format errors and submit them via perror()
 */
void error(char *str, ...)
{
    va_list vptr;
    char buf[BUFSZ];

    va_start(vptr, str);
    vsprintf (buf, str, vptr);
    va_end(vptr);

    perror(buf);
}



void insert_sort (array, count)
int array[], count;
{
    register int a, b;
    int t;

    for (a=1; a<count; ++a) {
        t = array[a];
        b = a-1;
        while (b>=0 && RE[t].type<RE[array[b]].type) {
            array[b+1] = array[b];
            --b;
        }
        array[b+1] = t;
    }
}



/*
 * Push "captures" into the state machine.
 *  i.e. '(a){2,3}' becomes '(a)aa'
 */
void dup_capture(pat, j, num)
int *pat, *j, num;
{
    register int k, t, s;

    t = *j-2;

    for (k=0; t && k != 1; --t) {
        if (pat[t] == END_CAPTURE)
            --k;
        else if (pat[t] == START_CAPTURE)
            ++k;
    }

    s = t+1;
    t = *j;

    while (num--)
        for (k=s; k<t; ++k)
            pat[(*j)++] = pat[k];
}


/*
 * Get the starting index of a "capture".  Note
 *  that these beasts can be nested.  e.g. '(ab(cd))'
 */
int get_start_index(i)
int i;
{
    register int t, j;

    if (RE[i].type == END_CAPTURE) {
        for (j=0, t=i-1; t && j != 1; --t) {
            if (RE[t].type == END_CAPTURE)
                --j;
            else if (RE[t].type == START_CAPTURE)
                ++j;
        }
        i = t+1;
    }

    return(i);
}



/*
 * Add a "next state" pointer to the state machine
 */
void add_next_ptr(i, val)
int i, val;
{
    if (RE[i].num >= RE[i].max_next)
        get_more_next_ptrs(&RE[i]);
    RE[i].next[RE[i].num++] = val;
}



/*
 * Allocate more "next state" pointers
 */
void get_more_next_ptrs(ptr)
struct _RE *ptr;
{
    register int i;

    ptr->max_next += NUM_RE_STATES;

    if (!(ptr->next = (int *)realloc(ptr->next, ptr->max_next* sizeof(int)))) {
        error("get_more_next_ptrs: Can't realloc");
        exit(1);
    }

    for (i=ptr->max_next-NUM_RE_STATES; i<ptr->max_next; ++i)
        ptr->next[i] = 0;
}



/*
 * Allocate more "states"
 */
void get_more_re_states()
{
    register int i, j;

    num_re_states += NUM_RE_STATES;

    if (!(RE = (struct _RE *)realloc(RE, num_re_states * sizeof(struct _RE)))) {
        error("get_more_re_states: Can't realloc");
        exit(1);
    }

    for (i=num_re_states-NUM_RE_STATES; i<num_re_states; ++i) {
        if (!(RE[i].next = (int *)malloc(NUM_NEXT_PTRS * sizeof(int)))) {
            error("get_more_re_states: Can't malloc");
            exit(1);
        }
        RE[i].max_next = NUM_NEXT_PTRS;
        RE[i].type = RE[i].capind = RE[i].special = 0;
        RE[i].num = RE[i].backind = 0;
        for (j=0; j<NUM_NEXT_PTRS; ++j)
            RE[i].next[j] = 0;
    }
}



/*
 * Get the initial memory
 */
void init_re()
{
    register int i, j;

    if (!(RE = (struct _RE *)malloc(num_re_states * sizeof(struct _RE)))) {
        error("init_re: Can't malloc");
        exit(1);
    }


    for (i=0; i<NUM_RE_STATES; ++i) {
        if (!(RE[i].next = (int *)malloc(NUM_NEXT_PTRS * sizeof(int)))) {
            error("init_re: Can't malloc");
            exit(1);
        }
        RE[i].max_next = NUM_NEXT_PTRS;
        RE[i].type = RE[i].capind = RE[i].special = 0;
        RE[i].num = RE[i].backind = 0;
        for (j=0; j<NUM_NEXT_PTRS; ++j)
            RE[i].next[j] = 0;
    }
}



/*
 * Handle "min max" RE's.  e.g. 'a{2,3}'
 */
int do_min_max(str, min, max)
char *str;
int *min, *max;
{
    int i=0;

    *min = *max = 0;

    if (!str[i] || !isdigit(str[i]))
        return(0);

    while (str[i] && str[i] != ',' && str[i] != '}') {
        if (!isdigit(str[i]))
            return(0);
        *min = *min*10 + str[i]-'0';
        ++i;
    }

    if (!str[i])
        return(0);
    else if (str[i] == '}')
        *max = *min;
    else {
        ++i;
        if (str[i] && str[i] == '}') {
            *max = -1;
            return(i);
        }

        while (str[i] && str[i] != '}') {
            if (!isdigit(str[i]))
                return(0);
            *max = *max*10 + str[i]-'0';
            ++i;
        }
    }
    return(i);
}



/*
 * Allow for true escapes, as well as convenience ones (perl style)
 *
 *  \t -- tab
 *  \d -- digit [0-9]
 *  \D -- not digit [^0-9]
 *  \w -- alphanumeric [a-zA-Z0-9_]
 *  \W -- not alphanumeric [^a-zA-Z0-9_]
 *  \s -- white space [ \t]
 *  \S -- not white space [^ \t]
 */
int do_escapes(ch, pat, i, j)
char *ch;
int *pat, *i, *j;
{
    int tmp=0;

    if (*ch >= '1' && *ch <= '9') {
        pat[(*j)++] = BACKREFERENCE;
        pat[(*j)++] = *ch;
        return(0);
    }

    switch(*ch) {
        case 't':
            pat[(*j)++] = '\t';
            break;
        case '*':
            pat[(*j)++] = LITERAL_STAR;
            break;
/*
        case '0':
            if (ch[1] && ch[2] && (ch[1] >= '0' && ch[1] <= '7')
                && (ch[2] >= '0' && ch[2] <= '7')) {
                pat[(*j)++] = (ch[1]-'0')*8 + (ch[2]-'0');
                *i += 2;
            }
            else
                return(1);
            break;
*/
        case 'd':
            if (make_set(&tmp, j, strlen("[0-9]"), "[0-9]", pat))
                return(1);
            break;
        case 'D':
            if (make_set(&tmp, j, strlen("[^0-9]"), "[^0-9]", pat))
                return(1);
            break;
        case 'w':
            if (make_set(&tmp, j, strlen("[a-zA-Z0-9_]"), "[a-zA-Z0-9_]", pat))
                return(1);
            break;
        case 'W':
            if (make_set(&tmp, j, strlen("[^a-zA-Z0-9_]"), "[^a-zA-Z0-9_]", pat))
                return(1);
            break;
        case 's':
            if (make_set(&tmp, j, strlen("[ \t]"), "[ \t]", pat))
                return(1);
            break;
        case 'S':
            if (make_set(&tmp, j, strlen("[^ \t]"), "[^ \t]", pat))
                return(1);
            break;
        default:
            pat[(*j)++] = *ch;
            break;
    }
    return(0);
}



/*
 * Given a set, modify the pattern space to
 *  reflect the longhand notation
 */
int make_set(i, j, len, input_pat, pat)
int *i, *j, len;
char input_pat[];
int *pat;
{
    int c;

    if (*i+1 >= len)
        return(1);

    if (input_pat[*i+1] == '^') {
        pat[(*j)++] = NEGSET;
        ++*i;
    }
    else
        pat[(*j)++] = SET;

    if (++*i >= len)
        return(1);

    /*
     * Allow the first character of the set to be ']' or '-'
     */
    if (input_pat[*i] == ']' || input_pat[*i] == '-')
        pat[(*j)++] = input_pat[(*i)++];

    while(*i < len && input_pat[*i] != ']') {
        if (input_pat[*i] == '-') {
            if (*i+1 >= len)
                return(1);
            if (input_pat[*i+1] == ']') {
                pat[(*j)++] = '-';
                ++*i;
                break;
            }
            for (c = input_pat[*i-1]+1; c<=input_pat[*i+1]; ++c)
                pat[(*j)++] = c;
            ++*i;
        }
        else if (input_pat[*i] == '\\') {
            if (*i+1 >= len)
                return(1);
            switch(input_pat[*i+1]) {
                case 't':
                    pat[(*j)++] = '\t';
                    break;
                default:
                    pat[(*j)++] = input_pat[*i+1];
                    break;
            }
            ++*i;
        }
        else
            pat[(*j)++] = input_pat[*i];
        ++*i;
    }

    if (*i >= len)
        return(1);

    pat[(*j)++] = END_SET;
    return(0);
}



/*
 * The meat is here.  Given an RE, create the state
 *  machine.  This requires 2 passes, one for parsing,
 *  expanding sets, and inserting capture points.  The
 *  second pass creates all non-deterministic links
 *  so that any state can have multiple transitions.  A
 *  lot of work (some of it mystical :-) goes into this
 *  elaborate data structure, but once it's complete the
 *  match() function becomes trivial.  This structure
 *  will totally smoke GNU egrep on complicated patterns
 *  such as the khadafy.regexp, but I clearly get smoked
 *  on the simple ones.  Perhaps if I write some sort of
 *  complexity evaluator and choose either the standard
 *  regcomp structure or this one -- best of both worlds???
 *
 * This function needs to be simplified.
 *
 */
int compile_pattern(input)
char *input;
{
    /* char pat_buffer[BUFSZ], tmp_buf[BUFSZ]; */
    char *input_pat;
    int *tmp_buf, *pat_buffer;
    int len, i, j, k, t, tmp, sp;
    int min, max, capnum, num;
    int capstack[BUFSZ];

    if (!(pat_buffer = (int *)malloc(BUFSZ * sizeof(int)))) {
        error("compile_pattern: Can't malloc");
        exit(1);
    }
    if (!(tmp_buf = (int *)malloc(BUFSZ * sizeof(int)))) {
        error("compile_pattern: Can't malloc");
        exit(1);
    }

    len = strlen(input);
    if (len == 0 || input[0] == '*' || input[0] == '+')
        return(1);

    if (!(input_pat = (char *)malloc(len + 1))) {
        error("compile_pattern: Can't malloc");
        exit(1);
    }
    strcpy(input_pat, input);

    i = 0;

    pat_buffer[0] = START_STATE;
    capnum = sp = 0;

    for (j=1; i<len; ++i) {
        switch(input_pat[i]) {
            case '\\':
                if (i+1 >= len)
                    return (1);
                if (do_escapes(&input_pat[i+1], pat_buffer, &i, &j))
                    return(1);
                ++i;
                break;
            case '?':
            case '{':
                if (input_pat[i] == '{') {
                    if (!(t=do_min_max(&input_pat[i+1], &min, &max))) {
                        pat_buffer[j++] = input_pat[i];
                        break;
                    }
                    if (min == 0 && max == 0)
                        return(1);
                }
                else {
                    min = 0;
                    max = 1;
                    t = -1;
                }

                if (max != -1 && min > max)
                    return(1);

                if (pat_buffer[j-1] == END_CAPTURE) {
                    if (max > 0) {
                        dup_capture(pat_buffer, &j, max-1);
                        pat_buffer[j++] = MIN_MAX;
                        pat_buffer[j++] = min;
                        pat_buffer[j++] = max;
                    }
                    else {
                        dup_capture(pat_buffer, &j, min);
                        pat_buffer[j++] = '*';
                    }
                }
                else {
                    tmp = pat_buffer[j-1];

                    if (max > 0) {
                        for (k=0; k<max-1; ++k)
                            pat_buffer[j++] = tmp;
                        pat_buffer[j++] = MIN_MAX;
                        pat_buffer[j++] = min;
                        pat_buffer[j++] = max;
                    }
                    else {
                        for (k=0; k<min; ++k)
                            pat_buffer[j++] = tmp;
                        pat_buffer[j++] = '*';
                    }
                }
                i += t+1;
                break;
            case '(':
                if (capnum >= NUM_CAPTURES || sp < 0 || sp >= NUM_CAPTURES)
                    return(1);
                capstack[sp++] = capnum;
                pat_buffer[j++] = START_CAPTURE;
                pat_buffer[j++] = ++capnum;
                break;
            case ')':
                if (sp < 1 || sp > NUM_CAPTURES+1)
                    return(1);
                --sp;
                pat_buffer[j++] = END_CAPTURE;
                break;
            case '.':
                if (i+1 < len && input_pat[i+1] == '*') {
                    pat_buffer[j++] = ANYTHING;
                    ++i;
                }
                else
                    pat_buffer[j++] = ANY_CHAR;
                break;
            case '^':
                if (i == 0)
                    pat_buffer[j++] = BEGIN_LINE;
                else
                    pat_buffer[j++] = input_pat[i];
                break;
            case '$':
                if (i+1 == len)
                    pat_buffer[j++] = END_LINE;
                else
                    pat_buffer[j++] = input_pat[i];
                break;
            case '[':
                if (make_set(&i, &j, len, input_pat, pat_buffer))
                    return(1);
                break;
            case '+':
                t = j-1;
                if (pat_buffer[t] == END_SET) {
                    for (--t; pat_buffer[t] != SET && pat_buffer[t] != NEGSET; --t)
                        ;
                    tmp = j-t;
                    for (k=0; k<tmp; ++k)
                        pat_buffer[j++] = pat_buffer[t+k];
                    --j;
                }
                else if (pat_buffer[t] == END_CAPTURE) {
                    dup_capture(pat_buffer, &j, 1);
                    --j;
                }
                else {
                    pat_buffer[j] = pat_buffer[j-1];
                }

                pat_buffer[j+1] = '*';
                j += 2;
                break;
            default:
                pat_buffer[j++] = input_pat[i];
        }
    }

    if (sp != 0)
        return(1);

    pat_buffer[j++] = ACCEPT_STATE;
    len = j;


    for (i=num=0; i<len; ++i) {
        if (num >= num_re_states)
            get_more_re_states();

        if (pat_buffer[i] == '*') {
            RE[num-1].special = '*';
            if (RE[num-1].type == END_CAPTURE) {
                for (j=0, k=num-2; k && j != 1; --k) {
                    if (RE[k].type == END_CAPTURE)
                        --j;
                    else if (RE[k].type == START_CAPTURE)
                        ++j;
                }
                tmp = k+1;
            }
            else {
                tmp = num-1;
            }

            add_next_ptr(num-1, tmp);
            continue;
        }
        else if (pat_buffer[i] == MIN_MAX) {
            min = pat_buffer[++i];
            max = pat_buffer[++i];

            for (k=0; k<max-min; ++k) {
                tmp = num;
                for (t=0; t<max-min-k; ++t)
                    tmp = get_start_index(tmp-1);

                add_next_ptr(tmp-1, num);

                while (tmp > 0 && RE[tmp].special == '*') {
                    tmp = get_start_index(tmp);
                    add_next_ptr(tmp-1, num);
                    --tmp;
                }
            }
            continue;
        }


        RE[num].type = (pat_buffer[i] == LITERAL_STAR) ? '*' : pat_buffer[i];

        if (pat_buffer[i] == SET || pat_buffer[i] == NEGSET) {
            j = 0;
            while ((tmp_buf[j++] = pat_buffer[++i]) != END_SET)
                ;
            if (!(RE[num].set = (char *)malloc(j))) {
                error("compile_pattern: Can't malloc");
                exit(1);
            }
            for (t=0; t<j-1; ++t)
                RE[num].set[t] = tmp_buf[t];
            RE[num].set[j-1] = '\0';
        }
        else if (pat_buffer[i] == START_CAPTURE) {
            RE[num].capind = pat_buffer[++i] - 1;
            capstack[sp++] = RE[num].capind;
        }
        else if (pat_buffer[i] == END_CAPTURE)
            RE[num].capind = capstack[--sp];
        else if (pat_buffer[i] == BACKREFERENCE)
            RE[num].backind = pat_buffer[++i] - '1';

        RE[num].num = 0;

        if (num != 0) {
            tmp = num-1;
            add_next_ptr(tmp, num);

            while (tmp > 0 && RE[tmp].special == '*') {
                tmp = get_start_index(tmp);
                add_next_ptr(tmp-1, num);
                --tmp;
            }
        }
        ++num;
    }

    for (i=0; i<num; ++i)
        insert_sort(RE[i].next, RE[i].num);

    return(0);
}



/*
 * Here we match the input text.  Just look at the states,
 *  and recursively follow the links -- easy.
 */
int match(i, index)
int i, index;
{
    int j, s, t, t_index, slen, found;

    for (j=0; i<=Textlen && j<RE[index].num; ++j) {
        t_index = RE[index].next[j];

        if (RE[t_index].type == ACCEPT_STATE) {
            return(1);
        }
        else if (RE[t_index].type == END_LINE) {
            if (i == Textlen)
                if (match(i, t_index))
                    return(1);
        }
        else if (RE[t_index].type == BEGIN_LINE) {
            if (i == 0)
                if (match(i, t_index))
                    return(1);
        }
        else if (RE[t_index].type == ANYTHING) {
            for (t=Textlen; t>=i; --t)
                if (match(t, t_index))
                    return(1);
        }
        else if ((RE[t_index].type == ANY_CHAR) ||
                 (i<Textlen && RE[t_index].type == Text[i])) {
            if (match(i+1, t_index))
                return(1);
        }
        else if ((RE[t_index].type == SET) ||
                 (RE[t_index].type == NEGSET)) {
            slen = strlen(RE[t_index].set);
            for (found=s=0; s<slen && !found; ++s)
                if (RE[t_index].set[s] == Text[i])
                    found = 1;
            if ((RE[t_index].type == SET && found) ||
                (RE[t_index].type == NEGSET && !found)) {
                if (match(i+1, t_index))
                    return(1);
            }
        }
        else if (RE[t_index].type == BACKREFERENCE) {
            t = RE[t_index].backind;
            if (Cap_start[t] != -1 && Cap_end[t] != -1 &&
                !strncmp(Text+i, Text+Cap_start[t],
                         Cap_end[t]-Cap_start[t]+1)) {
                if (match(i+Cap_end[t]-Cap_start[t]+1, t_index))
                    return(1);
            }
        }
        else if (RE[t_index].type == START_CAPTURE) {
            Cap_start[RE[t_index].capind] = i;
            if (match(i, t_index)) {
                Cap_start[RE[t_index].capind] = i;
                return(1);
            }
        }
        else if (RE[t_index].type == END_CAPTURE) {
            Cap_end[RE[t_index].capind] = i-1;
            if (match(i, t_index)) {
                Cap_end[RE[t_index].capind] = i-1;
                return(1);
            }
        }
    }
    return(0);
}



/*
 * Attempt to find a match.
 */
int do_re()
{
    int i=0;

    do {
        if (match(i, 0))
            return(1);
        if (RE[0].special == BEGIN_LINE)
            return(0);
    } while (++i < Textlen);
    return(0);
}



/*
 * Debug routine to show the "state machine"
 */
void show_re()
{
    int i,j;

    printf ("-------------------------------\n\n");
    printf ("         Compiled RE\n\n");

    for (i=0; RE[i].type != ACCEPT_STATE; ++i) {
        switch(RE[i].type) {
            case BEGIN_LINE:
                printf ("State:BEGIN_LINE(%d)", i);
                break;
            case END_LINE:
                printf ("State:END_LINE(%d)", i);
                break;
            case SET:
                printf ("State:SET(%d)", i);
                break;
            case NEGSET:
                printf ("State:NEGSET(%d)", i);
                break;
            case START_STATE:
                printf ("State:START_STATE(%d)", i);
                break;
            case ANY_CHAR:
                printf ("State:ANY_CHAR(%d)", i);
                break;
            case ANYTHING:
                printf ("State:ANYTHING(%d)", i);
                break;
            case ACCEPT_STATE:
                printf ("State:ACCEPT_STATE(%d)", i);
                break;
            case START_CAPTURE:
                printf ("State:START_CAPTURE(%d)", i);
                break;
            case END_CAPTURE:
                printf ("State:END_CAPTURE(%d)", i);
                break;
            case BACKREFERENCE:
                printf ("State:BACKREFERENCE(%d)", i);
                break;
            default:
                printf ("State:'%c'(%d)", RE[i].type, i);
                break;
        }

        printf (", num transitions %d", RE[i].num);

        if (RE[i].type == SET || RE[i].type == NEGSET)
            printf ("\n\tSet:<%s>", RE[i].set);

        for (j=0; j<RE[i].num; ++j) {
            printf ("\n\t");
            switch(RE[RE[i].next[j]].type) {
                case BEGIN_LINE:
                    printf ("Next state:BEGIN_LINE(%d)", RE[i].next[j]);
                    break;
                case END_LINE:
                    printf ("Next state:END_LINE(%d)", RE[i].next[j]);
                    break;
                case SET:
                    printf ("Next state:SET(%d)", RE[i].next[j]);
                    break;
                case NEGSET:
                    printf ("Next state:NEGSET(%d)", RE[i].next[j]);
                    break;
                case START_STATE:
                    printf ("Next state:START_STATE(%d)", RE[i].next[j]);
                    break;
                case ANY_CHAR:
                    printf ("Next state:ANY_CHAR(%d)", RE[i].next[j]);
                    break;
                case ANYTHING:
                    printf ("Next state:ANYTHING(%d)", RE[i].next[j]);
                    break;
                case ACCEPT_STATE:
                    printf ("Next state:ACCEPT_STATE(%d)", RE[i].next[j]);
                    break;
                case START_CAPTURE:
                    printf ("Next state:START_CAPTURE(%d)", RE[i].next[j]);
                    break;
                case END_CAPTURE:
                    printf ("Next state:END_CAPTURE(%d)", RE[i].next[j]);
                    break;
                case BACKREFERENCE:
                    printf ("Next state:BACKREFERENCE(%d)", RE[i].next[j]);
                    break;
                default:
                    printf ("Next state:'%c'(%d)", RE[RE[i].next[j]].type, RE[i].next[j]);
                    break;
            }
        }
        printf("\n");
    }
    printf ("\n-------------------------------\n");
}



/*
 * My own strpbrk()
 *
 * Return a pointer to the first occurrence in string "s"
 *  of any character from string "brk", or a NULL pointer
 *  if no character from "brk" exists in "s"
 */
static char *my_strpbrk(s, brk)
register char *s, *brk;
{
    register char *p;
    register char c;

    while ((c = *s)) {
        for (p = brk; *p; p++)
            if (c == *p)
                return (s);
        s++;
    }
    return (0);
}



/*
 * My own strstr()
 *
 * Find the first occurrence of "find" in "s".
 */
static char *my_strstr(s, find)
register char *s, *find;
{
    register char c, sc;
    register int len;

    if ((c = *find++) != 0) {
        len = strlen(find);
        do {
            do {
                if ((sc = *s++) == 0)
                    return (NULL);
            } while (sc != c);
        } while (strncmp(s, find, len) != 0);
        s--;
    }
    return ((char *)s);
}



/*
 * Driving routine.  Reads input lines, handles debug, etc.
 */
int dgrepit(fp, file, pat, debug, dlevel, negate, simple, num)
FILE *fp;
char *file, *pat;
int debug, dlevel, negate, simple, num;
{
    register int i;
    int retcode;
    char *rptr;
    int linum = 0, status = 1;

    while(fgets(Text, BUFSZ, fp)) {
        ++linum;
        Textlen = strlen(Text)-1;
        Text[Textlen] = '\0';

        if (simple) {
            if (debug) {
                if (file)
                    printf ("%s:", file);
                if (dlevel > 0)
                    puts(Text);
                if (my_strstr(Text, pat))
                    printf ("%d:** Simple Accept **\n", linum);
                else
                    printf ("%d:** Simple Reject **\n", linum);
            }
            else {
                rptr = my_strstr(Text, pat);
                if ((negate && !rptr) || (!negate && rptr)) {
                    if (file)
                        printf ("%s:", file);
                    if (num)
                        printf ("%d:", linum);
                    puts(Text);
                    status = 0;
                }
            }
            continue;
        }

        for (i=0; i<NUM_CAPTURES; ++i)
            Cap_start[i] = Cap_end[i] = -1;

        if (debug) {
            if (file)
                printf ("%s:", file);
            if (dlevel > 0)
                puts(Text);

            if (do_re()) {
                printf ("%d:** Accept **\n", linum);
                for (i=0; i<NUM_CAPTURES; ++i)
                    if (Cap_start[i] != -1 && Cap_end[i] != -1)
                        printf ("\tcapture %d [%.*s]\n", i+1,
                                 Cap_end[i]-Cap_start[i]+1,
                                 Text+Cap_start[i]);
            }
            else
                printf ("%d:** Reject **\n", linum);
        }
        else {
            retcode = do_re();
            if ((negate && !retcode) || (!negate && retcode)) {
                if (file)
                    printf ("%s:", file);
                if (num)
                    printf ("%d:", linum);
                puts(Text);
                status = 0;
            }
        }
    }
    return(status);
}



/*
 * Stupid main() for now, add getopt() and other fluff later.
 *
 *  Current options:
 *    -f file_containing_RE
 *    -v negate the search
 *    -n show the line numbers and file
 *    -d debug
 */
int main(argc, argv)
int argc;
char **argv;
{
    FILE *fp;
    char tmp[BUFSZ], *pat = NULL;
    register int i, j;
    int debug, negate, simple, dlevel, num, show_name, status;

    debug = negate = simple = num = dlevel = status = 0;
    init_re();

    for (i=1; i<argc; ++i) {
        if (!strncmp(argv[i], "-d", 2)) {
            debug = 1;
            if (strlen(argv[i]) > 2)
                dlevel = atoi(argv[i]+2);
        }
        else if (!strcmp(argv[i], "-h")) {
            fprintf (stderr, "Usage: %s [-v] [-n] [-f file] [-d[#]] [files ...]\n", argv[0]);
            exit(1);
        }
        else if (!strcmp(argv[i], "-v"))
            negate = 1;
        else if (!strcmp(argv[i], "-n"))
            num = 1;
        else if (!strcmp(argv[i], "-f")) {
            if (!(fp = fopen(argv[++i], "r"))) {
                error("%s: Can't read \"%s\"", argv[0], argv[i]);
                exit(2);
            }

            fgets(tmp, BUFSZ, fp);
            tmp[strlen(tmp)-1] = '\0';
            pat = tmp;
            ++i;
            break;
        }
        else {
            pat = argv[i++];
            break;
        }
    }


    if (!pat) {
        fprintf (stderr, "Usage: %s [-v] [-n] [-f file] [-d[#]] [files ...]\n", argv[0]);
        exit(2);
    }

    if (compile_pattern(pat)) {
        fprintf (stderr, "** malformed RE\n");
        exit(2);
    }
    if (!my_strpbrk(pat, "^.*+?[]{}\\$()"))
        simple = 1;

    if (debug) {
        printf ("\npattern:%s\n", pat);
        show_re();
    }

    if (i == argc)
        status = dgrepit(stdin, 0, pat, debug, dlevel, negate, simple, num);
    else {
        show_name = (argc - i > 1);
        for (j=i; j<argc; ++j) {
            if (!(fp = fopen(argv[j], "r"))) {
                error("%s: Can't read \"%s\"", argv[0], argv[j]);
                continue;
            }
            if (dgrepit(fp, show_name?argv[j]:0, pat, debug, dlevel, negate, simple, num))
                status = 1;
            fclose(fp);
        }
    }
    exit(status);
}
