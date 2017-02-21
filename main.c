#include <stdio.h>
#include <stdlib.h>

#include <editline/readline.h>
#include "mpc.h"

/* Macros */

#define UPTO(count) \
  for(int i = 0; i < (count); i++)

#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_free(args); \
    return err; \
  }

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i. Got %s, Expected %s.", \
    func, index, ltype2name(args->cell[index]->type), ltype2name(expect))

#define LASSERT_NUM(func, args, num) \
  LASSERT(args, args->count == num, \
    "Function '%s' passed incorrect number of arguments. Got %i, Expected %i.", \
    func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
  LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed empty argument %i.", func, index);

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

  lbuiltin builtin;
  lenv* env;
  lval* formals;
  lval* body;

  int count;
  lval** cell;
};

struct lenv {
  lenv* parent;
  int count;
  char** syms;
  lval** vals;
};

/* Function signatures */
/* Only the mandatory ones with cross-references */

void lval_free(lval* v);
void lval_print(lval* v);
lval* lval_eval(lenv* e, lval* v);
lenv* lenv_new(void);
lenv* lenv_copy(lenv* e);
void lenv_free(lenv* e);
void lenv_put(lenv* e, lval* k, lval* v);
lval* builtin_eval(lenv* e, lval* a);

/* Helpers */

char* ltype2name(int t) {
  switch(t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

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

lval* lval_err(char* fmt, ...) {
  lval* v = lval_new(LVAL_ERR);
  va_list va;
  va_start(va, fmt);
  v->err = malloc(512);
  vsnprintf(v->err, 511, fmt, va);
  v->err = realloc(v->err, strlen(v->err)+1);
  va_end(va);
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
  v->builtin = func;
  return v;
}

lval* lval_lambda(lval* formals, lval* body) {
  lval* v = lval_new(LVAL_FUN);
  v->builtin = NULL;
  v->env = lenv_new();
  v->formals = formals;
  v->body = body;
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
    case LVAL_FUN: 
      if (v->builtin) {
        x->builtin = v->builtin;
      } else {
        x->builtin = NULL;
        x->env = lenv_copy(v->env);
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
      }
    break;
    
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
    case LVAL_FUN: 
      if (!v->builtin) {
        lenv_free(v->env);
        lval_free(v->formals);
        lval_free(v->body);
      }
    break;
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

lval* lval_call(lenv* e, lval* f, lval* a) {
  if (f->builtin) { return f->builtin(e, a); }

  int given = a->count;
  int total = f->formals->count;

  while (a->count) {
    if (f->formals->count == 0) {
      lval_free(a);
      return lval_err("Function passed too many arguments. Got %i, Expected %i.", given, total);
    }

    lval* sym = lval_pop(f->formals, 0);
    lval* val = lval_pop(a, 0);
    
    lenv_put(f->env, sym, val);
    lval_free(sym); lval_free(val);
  }

  lval_free(a);

  if (f->formals->count == 0) {
    f->env->parent = e;
    return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    return lval_copy(f);
  }
}

/* Env contructor */

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->parent = NULL;
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
  if (e->parent) {
    return lenv_get(e->parent, k);
  } else {
    return lval_err("Unknown symbol '%s' !", k->sym);
  }
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

void lenv_global_put(lenv* e, lval* k, lval* v) {
  while (e->parent) { e = e->parent; }
  lenv_put(e, k, v);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_free(k); lval_free(v);
}

lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n->parent = e->parent;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  UPTO(e->count) {
    n->syms[i] = malloc(strlen(e->syms[i])+1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
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
    case LVAL_FUN: 
      if (v->builtin) {
        printf("<builtin-function>");
      } else {
        printf("(fun ");
        lval_print(v->formals);
        putchar(' ');
        lval_print(v->body);
        putchar(')');
      }
    break;
    case LVAL_SEXPR: lval_print_expr(v, '(', ')'); break;
    case LVAL_QEXPR: lval_print_expr(v, '{', '}'); break;
  }
}

void lval_println(lval* v) {
  lval_print(v); putchar('\n');
}

/* Builtins */

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval* syms = a->cell[0];

  UPTO(syms->count) {
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM), "Function '%s' cannot define non-symbol! Got %s, expected %s.", func, ltype2name(syms->cell[i]->type), ltype2name(LVAL_SYM));
  }

  LASSERT(a, syms->count == a->count-1, "Function '%s' needs a value for each symbol!", func);

  UPTO(syms->count) {
    if (strcmp(func, "def")==0) {
      lenv_global_put(e, syms->cell[i], a->cell[i+1]);
    }
    if (strcmp(func, "=")==0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }

  lval_free(a);
  return lval_sexpr();
}

lval* builtin_def(lenv* e, lval* a) {
  return builtin_var(e, a, "def");
}

lval* builtin_set(lenv* e, lval* a) {
  return builtin_var(e, a, "=");
}

lval* builtin_lambda(lenv* e, lval* a) {
  LASSERT_NUM("fun", a, 2);
  LASSERT_TYPE("fun", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("fun", a, 1, LVAL_QEXPR);

  UPTO(a->cell[0]->count) {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM), "Cannot define non-symbol. Got %s, expected %s.", ltype2name(a->cell[0]->cell[i]->type), ltype2name(LVAL_SYM));
  }

  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  lval_free(a);

  return lval_lambda(formals, body);
}

lval* builtin_head(lenv* e, lval* a) {
  LASSERT(a, a->count==1, "Function 'head' wrong numberof arguments! Got %i, expected 1.", a->count);
  LASSERT(a, a->cell[0]->type==LVAL_QEXPR, "Function 'head' passed incorrect type! Got %s, expected %s.", ltype2name(a->cell[0]->type), ltype2name(LVAL_QEXPR));
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
    lval* err = lval_err("S-Expression starts with incorrect type. Got %s, Expected %s.", ltype2name(f->type), ltype2name(LVAL_FUN));
    lval_free(v); lval_free(f);
    return err;
  }

  lval* result = lval_call(e, f, v);
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
  lenv_add_builtin(e, "def", builtin_def); /* Global var */
  lenv_add_builtin(e, "=", builtin_set); /* Local var */
  lenv_add_builtin(e, "fun", builtin_lambda);
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

