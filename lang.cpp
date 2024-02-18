#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

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
    virtual ~ExprAST() = default;
    virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST{
  double Val;
  public:
    NumberExprAST(double val) : Val(Val){}
    Value *codegen() override;
};

class VariableExprAST: public ExprAST{
  std::string Name;
  public: 
    VariableExprAST(const std::string &Name) : Name(Name){}
    Value *codegen() override;
};

class BinaryExprAST: public ExprAST{
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

  public: 
    BinaryExprAST(char Op, 
    std::unique_ptr<ExprAST> LHS, 
    std::unique_ptr<ExprAST> RHS): 
    Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}

    Value *codegen() override;
};

class CallExprAST : public ExprAST{
  std::string Calle;
  std::vector<std::unique_ptr<ExprAST>> Args;

  public: 
    CallExprAST(const std::string &Calle, 
    std::vector<std::unique_ptr<ExprAST>> Args) 
    : Calle(Calle), Args(std::move(Args)){}

    Value *codegen() override;
};

class ProtoTypeAST{
  std::string Name;
  std::vector<std::string> Args;

  public: 
    ProtoTypeAST(const std::string &Name, 
    std::vector<std::string> Args)
    : Name(Name), Args(Args){}

    Function *codegen();

    const std::string &getName() const { return Name; }

};

class FunctionAST{
  std::unique_ptr<ProtoTypeAST> Proto; 
  std::unique_ptr<ExprAST> Body;
  
  public:
    FunctionAST(std::unique_ptr<ProtoTypeAST> Proto, 
    std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {}

    Function *codegen();
};

static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS);

static int currTok;
static int getNextToken(){
  return currTok = gettok();
}

static std::unique_ptr<ExprAST> LogError(const char* Str){
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

static std::unique_ptr<ProtoTypeAST> LogErrorP(const char* Str){
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> parseNumberExpr(){
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr(){
  getNextToken();
  auto V = ParseExpression();
  if(!V) return nullptr;

  if(currTok != ')') return LogError("expected ')'");
  getNextToken();
  return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr(){
  std::string IdName = IdentifierStr;
  getNextToken();

  if (currTok != '('){
    return std::make_unique<VariableExprAST>(IdName);
  }

  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> Args; 
  if(currTok != ')'){
    while(true){
      if(auto Arg = ParseExpression()){
        Args.push_back(std::move(Arg));
      }
      else{
        return nullptr;
      }

      if (currTok == ')')
        break;

      if (currTok != ',') 
        return LogError("Expected ')' or ',' in argument list");

      getNextToken();
    }
  }

  getNextToken();
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary(){
  switch(currTok){
    default: 
      return LogError("unknown token when expecting an expression");
    case tok_identifier:
      return ParseIdentifierExpr();
    case tok_number:
      return parseNumberExpr();
    case '(':
      return ParseParenExpr();
  }
}

static std::unique_ptr<ExprAST> ParseExpression(){
  auto LHS = ParsePrimary();
  if(!LHS)
    return nullptr;
  
  return ParseBinOpRHS(0, std::move(LHS));
}

static std::map<char, int> BinOpPrecedence;

static int GetTokPrecedence(){
  if(!isascii(currTok))
    return -1;

  int TokPrec = BinOpPrecedence[currTok];
  if(TokPrec <= 0) return -1;
  return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS){
  while(true){
    int TokPrec = GetTokPrecedence();

    if(TokPrec < ExprPrec) 
      return LHS;
    
    int BinOp = currTok;
    getNextToken();

    auto RHS = ParsePrimary();
    if(!RHS)
      return nullptr;
    int NextPrec = GetTokPrecedence();
    if(TokPrec < NextPrec){
      RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
      if(!RHS)
        return nullptr;
    }
    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

static std::unique_ptr<ProtoTypeAST> ParsePrototype(){
  if(currTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");
  
  std::string FnName = IdentifierStr;
  getNextToken();

  if(currTok != '(')
    return LogErrorP("Expected '(' in prototype");
  std::vector<std::string> ArgName;
  while(getNextToken() == tok_identifier)
    ArgName.push_back(IdentifierStr);
  if(currTok != ')')
    return LogErrorP("Expected ')' in prototype");
  
  getNextToken();
  return std::make_unique<ProtoTypeAST>(FnName, std::move(ArgName));
}

std::unique_ptr<FunctionAST> ParseDefinition(){
  getNextToken();
  auto Proto = ParsePrototype();
  if(!Proto) return nullptr;
  if(auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr; 
}

static std::unique_ptr<ProtoTypeAST> ParseExtern(){
  getNextToken();
  return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr(){
  if(auto E = ParseExpression()){
    auto Proto = std::make_unique<ProtoTypeAST>("", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/*
=====CodeGen=====
*/

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder = std::unique_ptr<IRBuilder<>>(new IRBuilder<>(*TheContext));
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

Value *LogErrorV(const char *str){
  LogError(str);
  return nullptr;
}

Value *NumberExprAST::codegen(){
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen(){
  Value *V = NamedValues[Name];
  if(!V) LogErrorV("Unknown Variable Name");
  return V;
}

Value *BinaryExprAST::codegen(){
  Value *L = LHS->codegen();
  Value *R = LHS->codegen();

  if(!L || !R){
    return nullptr;
  }

  switch (Op)
  {
  case '+':
    return Builder->CreateFAdd(L,R,"addtmp");
  case '-':
    return Builder->CreateFSub(L,R,"subtmp");
  case '*':
    return Builder->CreateFMul(L,R,"multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    return LogErrorV("Invalid Binary operator");
  }
}

Value *CallExprAST::codegen(){
  Function *CalleeF = TheModule->getFunction(Calle);
  if(!CalleeF) return LogErrorV("Unknown function referenced");

  if(CalleeF->arg_size() != Args.size()) return LogErrorV("Incorrect # of arguments");

  std::vector<Value *> ArgsV;
  for(unsigned i = 0, e = Args.size(); i!=e; ++i){
    ArgsV.push_back(Args[i]->codegen());
    if(!ArgsV.back()) return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *ProtoTypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {
  // First, check for an existing function from a previous 'extern' declaration.
  Function *TheFunction = TheModule->getFunction(Proto->getName());

  if (!TheFunction)
    TheFunction = Proto->codegen();

  if (!TheFunction)
    return nullptr;

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = Body->codegen()) {
    // Finish off the function.
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    return TheFunction;
  }

  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return nullptr;
}


static void InitializeModule() {
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("my cool jit", *TheContext);

  // Create a new builder for the module.
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern: ");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read top-level expression:");
      FnIR->print(errs());
      fprintf(stderr, "\n");

      // Remove the anonymous expression.
      FnIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (currTok) {
    case tok_eof:
      return;
    case ';': // ignore top-level semicolons.
      getNextToken();
      break;
    case tok_func:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}
  
int main(){
  BinOpPrecedence['<'] = 10;
  BinOpPrecedence['+'] = 20;
  BinOpPrecedence['-'] = 30;
  BinOpPrecedence['*'] = 40;
  // while(true){
  //   int tok = gettok();
  //   cout << token_to_string(tok) << endl;
  // }
  fprintf(stderr, "ready> ");
  getNextToken();
  InitializeModule();
  // Run the main "interpreter loop" now.
  MainLoop();
  TheModule->print(errs(), nullptr);
  return 0;
}
