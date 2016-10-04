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
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

enum { 
  LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUN,
  LVAL_SEXPR, LVAL_QEXPR 
};

typedef lval*(*lbuiltin) (lenv*, lval*);

struct lval {
  int type;
  char* err;
  long num;
  char* sym;
  lbuiltin fun;

  int count;
  lval** cell;
};

struct lenv {
  int count;
  char** syms;
  lval** vals;
};

/* Function signatures */
/* Only the mandatory ones with cross-references */

void lval_free(lval* v);
void lval_print(lval* v);
lval* lval_eval(lenv* e, lval* v);

/* Lisp value constructors */

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

lval* lval_fun(lbuiltin func) {
  lval* v = lval_new(LVAL_FUN);
  v->fun = func;
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

/* Lisp value functions */

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

lval* lval_copy(lval* v) {
  lval* x = lval_new(v->type);

  switch (v->type) {
    case LVAL_NUM: x->num = v->num; break;
    case LVAL_FUN: x->fun = v->fun; break;
    
    case LVAL_ERR:
      x->err = malloc(strlen(v->err)+1);
      strcpy(x->err, v->err);
    break;

    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym)+1);
      strcpy(x->sym, v->sym);
    break;

    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      UPTO(x->count) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
    break;

  }

  return x;
}

void lval_free(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_FUN: break;
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

/* Env contructor */

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

void lenv_free(lenv* e) {
  UPTO(e->count) {
    free(e->syms[i]);
    lval_free(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

/* Env functions */

lval* lenv_get(lenv* e, lval* k) {
  UPTO(e->count) {
    if (strcmp(e->syms[i], k->sym)==0) {
      return lval_copy(e->vals[i]);
    }
  }
  return lval_err("Unknown symbol!");
}

void lenv_put(lenv* e, lval* k, lval* v) {
  UPTO(e->count) {
    if (strcmp(e->syms[i], k->sym)==0) {
      lval_free(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);
  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_free(k); lval_free(v);
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
    case LVAL_FUN: printf("<function>"); break;
    case LVAL_SEXPR: lval_print_expr(v, '(', ')'); break;
    case LVAL_QEXPR: lval_print_expr(v, '{', '}'); break;
  }
}

void lval_println(lval* v) {
  lval_print(v); putchar('\n');
}

/* Builtins */

lval* builtin_def(lenv* e, lval* a) {
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'def' passed incorrect type!");

  lval* syms = a->cell[0];

  UPTO(syms->count) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbol!");
  }

  LASSERT(a, syms->count == a->count-1, "Function 'def' needs a value for each symbol!");

  UPTO(syms->count) {
    lenv_put(e, syms->cell[i], a->cell[i+1]);
  }

  lval_free(a);
  return lval_sexpr();
}

lval* builtin_head(lenv* e, lval* a) {
  LASSERT(a, a->count==1, "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type==LVAL_QEXPR, "Function 'head' passed incorrect types!");
  LASSERT(a, a->cell[0]->count!=0, "Function 'head' passed {}!");

  lval* v = lval_take(a, 0);
  while (v->count > 1) {
    lval_free(lval_pop(v,1));
  }
  return v;
}

lval* builtin_tail(lenv* e, lval* a) {
  LASSERT(a, a->count==1, "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect types!");
  LASSERT(a, a->cell[0]->count!=0, "Function 'tail' passed {}!");

  lval* v = lval_take(a,0);
  lval_free(lval_pop(v,0));
  return v;
}

lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT(a, a->count==1, "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type==LVAL_QEXPR, "Function 'eval' passed incorrect types!");

  lval* x = lval_take(a,0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
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

lval* builtin_op(lenv* e, lval* a, char* op) {
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

lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

/* Eval */

lval* lval_eval_sexpr(lenv* e, lval* v) {
  UPTO(v->count) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  UPTO(v->count) {
    if (v->cell[i]->type == LVAL_ERR) {
      return lval_take(v,i);
    }
  }

  if (v->count==0) { return v; }

  if (v->count==1) { return lval_take(v, 0); }

  lval* f = lval_pop(v, 0);
  if (f->type!=LVAL_FUN) {
    lval_free(v); lval_free(f);
    return lval_err("First element is not a function!");
  }

  lval* result = f->fun(e, v);
  lval_free(f);
  return result;
}

lval* lval_eval(lenv* e, lval* v) {
  if (v->type==LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_free(v);
    return x;
  }
  if (v->type==LVAL_SEXPR) {
    return lval_eval_sexpr(e, v);
  }
  return v;
}

/* Add all builtins to env */

void lenv_add_builtins(lenv* e) {
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
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
        symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ; \
        sexpr : '(' <expr>* ')' ; \
        qexpr : '{' <expr>* '}' ; \
        expr : <number> | <symbol> | <sexpr> | <qexpr> ; \
        lispy : /^/ <expr>* /$/ ; \
      ",
      Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  puts("Lispy Version 0.0.1");
  puts("Press Ctrl+c to Exit\n");

  lenv* e = lenv_new();
  lenv_add_builtins(e);

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_free(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  lenv_free(e);

  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}

