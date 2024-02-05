#include<string>
#include<iostream>
#include<memory>
#include<vector>

using namespace std;

/*
=====Lexer=====
*/
enum Token{
  tok_eof = -1,
  tok_func = -2,
  tok_extern = -3,
  tok_identifier = -4,
  tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;
int age;

static int gettok(){
  static int LastChar = ' ';

  while(isspace(LastChar)){
    LastChar = getchar();
  }

  if(isalpha(LastChar)){
    IdentifierStr = LastChar;
    while(isalnum(LastChar = getchar())) IdentifierStr+=LastChar;
    if (IdentifierStr == "func") return tok_func;
    if (IdentifierStr == "extern") return tok_extern;
    return tok_identifier;
  } 

  if (isdigit(LastChar) || LastChar == '.'){
    std::string NumStr;
    do{
      NumStr+=LastChar;
      LastChar = getchar();
    }while(isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
  
  if(LastChar == '#'){
    do{
      LastChar = getchar();
    }while(LastChar != '\n' || LastChar != EOF || LastChar != '\r');
    
    if (LastChar != EOF) return gettok();
  }

  if(LastChar == EOF){
    return tok_eof;
  }

  int thisChar = LastChar;
  LastChar = getchar();
  return thisChar;
}

std::string token_to_string(int tok){
  switch (tok)
  {
    case -1:
      return "EOF_tok";
      break;
    case -2:
      return "func_tok";
      break;
    case -3:
      return "extern_tok";
      break;
    case -4:
      return "ident_tok";
      break;
    case -5:
      return "number_tok";
    default:
      return "not_known_tok";
      break;
  }
}

/*
=====Abstract Syntax Tree=====
*/
class ExprAST{
  public: 
    virtual ~ExprAST(){}
};

class NumberExprAST : public ExprAST{
  double Val;
  public:
    NumberExprAST(double val) : Val(Val){}
};

class VariableExprAST: public ExprAST{
  std::string Name;
  public: 
    VariableExprAST(const std::string &Name) : Name(Name){}
};

class BinaryExprAST: public ExprAST{
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

  public: 
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS): 
    Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST : public ExprAST{
  std::string Calle;
  std::vector<std::unique_ptr<ExprAST>> Args;

  public: 
    CallExprAST(const std::string &Calle, std::vector<std::unique_ptr<ExprAST>> Args) 
    : Calle(Calle), Args(std::move(Args)){}
};

class ProtoTypeAST{
  std::string Name;
  std::vector<std::string> Args;

  public: 
    ProtoTypeAST(const std::string &Name, std::vector<std::string> Args)
    : Name(Name), Args(Args){}

    const std::string &getName() const { return Name; }
};

class FunctionAST{
  std::unique_ptr<ProtoTypeAST> Proto; 
  std::unique_ptr<ExprAST> Body;

  FunctionAST(std::unique_ptr<ProtoTypeAST> Proto, std::unique_ptr<ExprAST> Body)
  : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
  
int main(){
  // while(true){
  //   int tok = gettok();
  //   cout << token_to_string(tok) << endl;
  // }
  return 1;
}

