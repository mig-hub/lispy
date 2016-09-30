#include <stdio.h>
#include <stdlib.h>

#include <editline/readline.h>
#include "mpc.h"

/* Macros */

#define UPTO(count) \
  for(int i = 0; i < (count); i++)

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_free(args); return lval_err(err);  }

/* Types */

enum { 
  LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR 
};

typedef struct lval {
  int type;
  char* err;
  long num;
  char* sym;
  int count;
  struct lval** cell;
} lval;

/* Function signatures */
/* Only the mandatory ones with cross-references */

void lval_free(lval* v);
void lval_print(lval* v);
lval* lval_eval(lval* v);

/* Lispy values */

lval* lval_new(int type) {
  lval* v = malloc(sizeof(lval));
  v->type = type;
  return v;
}

lval* lval_num(long x) {
  lval* v = lval_new(LVAL_NUM);
  v->num = x;
  return v;
}

lval* lval_err(char* m) {
  lval* v = lval_new(LVAL_ERR);
  v->err = malloc(strlen(m)+1);
  strcpy(v->err, m);
  return v;
}

lval* lval_sym(char* s) {
  lval* v = lval_new(LVAL_SYM);
  v->sym = malloc(strlen(s)+1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = lval_new(LVAL_SEXPR);
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval* lval_qexpr(void) {
  lval* v = lval_new(LVAL_QEXPR);
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  v->count--;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_free(v);
  return x;
}

void lval_free(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      UPTO(v->count) {
        lval_free(v->cell[i]);
      }
      free(v->cell);
    break;
  }
  free(v);
}

lval* lval_join(lval* x, lval* y) {
  while (y->count) {
    x = lval_add(x, lval_pop(y,0));
  }
  lval_free(y);
  return x;
}

/* Read */

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno!=ERANGE ?
    lval_num(x) : lval_err("Invalid number");
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) { 
    return lval_read_num(t); 
  }
  if (strstr(t->tag, "symbol")) { 
    return lval_sym(t->contents); 
  }

  lval* x = NULL;

  if (strcmp(t->tag, ">")==0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }
  UPTO(t->children_num) {
    if (strcmp(t->children[i]->contents, "(")==0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, ")")==0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, "{")==0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, "}")==0) {
      continue;
    }
    if (strcmp(t->children[i]->tag, "regex")==0) {
      continue;
    }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

/* Print */

void lval_print_expr(lval* v, char open, char close) {
  putchar(open);
  UPTO(v->count) {
    lval_print(v->cell[i]);
    if (i != (v->count - 1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_ERR: printf("Error: %s", v->err); break;
    case LVAL_NUM: printf("%li", v->num); break;
    case LVAL_SYM: printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_print_expr(v, '(', ')'); break;
    case LVAL_QEXPR: lval_print_expr(v, '{', '}'); break;
  }
}

void lval_println(lval* v) {
  lval_print(v); putchar('\n');
}

/* Builtins */

lval* builtin_head(lval* a) {
  LASSERT(a, a->count==1, "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type==LVAL_QEXPR, "Function 'head' passed incorrect types!");
  LASSERT(a, a->cell[0]->count!=0, "Function 'head' passed {}!");

  lval* v = lval_take(a, 0);
  while (v->count > 1) {
    lval_free(lval_pop(v,1));
  }
  return v;
}

lval* builtin_tail(lval* a) {
  LASSERT(a, a->count==1, "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect types!");
  LASSERT(a, a->cell[0]->count!=0, "Function 'tail' passed {}!");

  lval* v = lval_take(a,0);
  lval_free(lval_pop(v,0));
  return v;
}

lval* builtin_list(lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lval* a) {
  LASSERT(a, a->count==1, "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type==LVAL_QEXPR, "Function 'eval' passed incorrect types!");

  lval* x = lval_take(a,0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval* builtin_join(lval* a) {
  UPTO(a->count) {
    LASSERT(a, a->cell[i]->type==LVAL_QEXPR, "Function 'join' passed incorrect types!");
  }

  lval* x = lval_pop(a,0);
  while (a->count) {
    x = lval_join(x, lval_pop(a,0));
  }
  lval_free(a);
  return x;
}

lval* builtin_op(lval* a, char* op) {
  UPTO(a->count) {
    if (a->cell[i]->type!=LVAL_NUM) {
      lval_free(a);
      return lval_err("Cannot operate on non-number");
    }
  }

  lval* x = lval_pop(a, 0);

  if ((strcmp(op,"-")==0) && a->count==0) {
    x->num = -x->num;
  }

  while (a->count > 0) {
    lval* y = lval_pop(a, 0);

    if (strcmp(op, "+")==0) { x->num += y->num; }
    if (strcmp(op, "-")==0) { x->num -= y->num; }
    if (strcmp(op, "*")==0) { x->num *= y->num; }
    if (strcmp(op, "/")==0) { 
      if (y->num==0) {
        lval_free(x); lval_free(y);
        x = lval_err("Division by zero!");
        break;
      }
      x->num /= y->num;
    }
    lval_free(y);
  }
  lval_free(a);
  return  x;
}

lval* builtin(lval* a, char* func) {
  if (strcmp("list",func)==0) { return builtin_list(a); }
  if (strcmp("head",func)==0) { return builtin_head(a); }
  if (strcmp("tail",func)==0) { return builtin_tail(a); }
  if (strcmp("join",func)==0) { return builtin_join(a); }
  if (strcmp("eval",func)==0) { return builtin_eval(a); }
  if (strstr("+-/*",func)) { return builtin_op(a, func); }
  lval_free(a);
  return lval_err("Unknown function!");
}

/* Eval */

lval* lval_eval_sexpr(lval* v) {
  UPTO(v->count) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  UPTO(v->count) {
    if (v->cell[i]->type == LVAL_ERR) {
      lval_take(v,i);
    }
  }

  if (v->count==0) { return v; }

  if (v->count==1) { return lval_take(v, 0); }

  lval* f = lval_pop(v, 0);
  if (f->type!=LVAL_SYM) {
    lval_free(f); lval_free(v);
    return lval_err("S-expression does not start with symbol!");
  }

  lval* result = builtin(v, f->sym);
  lval_free(f);
  return result;
}

lval* lval_eval(lval* v) {
  if (v->type==LVAL_SEXPR) {
    return lval_eval_sexpr(v);
  }
  return v;
}

/* Main */

int main(int argc, const char *argv[])
{
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
      " \
        number : /-?[0-9]+/ ; \
        symbol : '+' | '-' | '*' | '/' | \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\" ; \
        sexpr : '(' <expr>* ')' ; \
        qexpr : '{' <expr>* '}' ; \
        expr : <number> | <symbol> | <sexpr> | <qexpr> ; \
        lispy : /^/ <expr>* /$/ ; \
      ",
      Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  puts("Lispy Version 0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_eval(lval_read(r.output));
      lval_println(x);
      lval_free(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}

