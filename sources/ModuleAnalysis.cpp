// Copyright 2012 by Laszlo Nagy [see file MIT-LICENSE]

#include "ModuleAnalysis.hpp"
#include "ScopeAnalysis.hpp"

#include <iterator>
#include <map>

#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>

#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/range.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm/transform.hpp>


namespace {

// Report function for pseudo constness analysis.
void ReportVariablePseudoConstness(clang::DiagnosticsEngine & DE, clang::VarDecl const * const V) {
    static char const * const Message = "variable '%0' could be declared as const";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
}

// Report function for debug functionality.
void ReportVariableDeclaration(clang::DiagnosticsEngine & DE, clang::VarDecl const * const V) {
    static char const * const Message = "variable '%0' declared here";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Note, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
    DB.setForceEmit();
}

void ReportFunctionDeclaration(clang::DiagnosticsEngine & DE, clang::FunctionDecl const * const F) {
    static char const * const Message = "function '%0' declared here";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Note, Message);
    clang::DiagnosticBuilder DB = DE.Report(F->getSourceRange().getBegin(), Id);
    DB << F->getNameAsString();
    DB.setForceEmit();
}


typedef std::set<clang::VarDecl const *> Variables;


// Pseudo constness analysis detects what variable can be declare as const.
// This analysis runs through multiple scopes. We need to store the state of
// the ongoing analysis. Once the variable was changed can't be const.
class PseudoConstnessAnalysisState : public boost::noncopyable {
public:
    PseudoConstnessAnalysisState()
        : boost::noncopyable()
        , Candidates()
        , Changed()
    { }

    void Eval(ScopeAnalysis const & Analysis, clang::VarDecl const * const V) {
        if (Analysis.WasChanged(V)) {
            Candidates.erase(V);
            Changed.insert(V);
        } else if (Analysis.WasReferenced(V)) {
            if (Changed.end() == Changed.find(V)) {
                if (! IsConst(*V)) {
                    Candidates.insert(V);
                }
            }
        }
    }

    void GenerateReports(clang::DiagnosticsEngine & DE) {
        boost::for_each(Candidates,
            boost::bind(ReportVariablePseudoConstness, boost::ref(DE), _1));
    }

private:
    static bool IsConst(clang::VarDecl const & D) {
        return (D.getType().getNonReferenceType().isConstQualified());
    }

private:
    Variables Candidates;
    Variables Changed;
};


// Represents a function declaration. This class try to add new methods
// to clang::FunctionDecl. That type is part of an inheritance tree.
// To add functions to it, here is a duplication of the hierarchy.
struct FunctionWrapper : public boost::noncopyable {
protected:
    virtual clang::FunctionDecl const * GetFunctionDecl() const = 0;

    typedef std::set<clang::VarDecl const *> VarDeclSet;
    virtual VarDeclSet GetArguments() const = 0;
    virtual VarDeclSet GetLocals() const = 0;
    virtual VarDeclSet GetMembers() const = 0;

public:
    // Debug functionality
    virtual void DumpFuncionDeclaration(clang::DiagnosticsEngine &) const;
    virtual void DumpArguments(clang::DiagnosticsEngine &) const;
    virtual void DumpLocalVariables(clang::DiagnosticsEngine &) const;
    virtual void DumpMemberVariables(clang::DiagnosticsEngine &) const;

    // Analysis functionality
    virtual void DumpVariableChanges(clang::DiagnosticsEngine &) const;
    virtual void DumpVariableUsages(clang::DiagnosticsEngine &) const;
    virtual void CheckPseudoConstness(PseudoConstnessAnalysisState &) const;
};

void FunctionWrapper::DumpFuncionDeclaration(clang::DiagnosticsEngine & DE) const {
    clang::FunctionDecl const * F = GetFunctionDecl();
    ReportFunctionDeclaration(DE, F);
}

void FunctionWrapper::DumpArguments(clang::DiagnosticsEngine & DE) const {
    boost::for_each(GetArguments(),
        boost::bind(ReportVariableDeclaration, boost::ref(DE), _1));
}

void FunctionWrapper::DumpLocalVariables(clang::DiagnosticsEngine & DE) const {
    boost::for_each(GetLocals(),
        boost::bind(ReportVariableDeclaration, boost::ref(DE), _1));
}

void FunctionWrapper::DumpMemberVariables(clang::DiagnosticsEngine & DE) const {
    boost::for_each(GetMembers(),
        boost::bind(ReportVariableDeclaration, boost::ref(DE), _1));
}

void FunctionWrapper::DumpVariableChanges(clang::DiagnosticsEngine & DE) const {
    clang::FunctionDecl const * F = GetFunctionDecl();
    ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
    Analysis.DebugChanged(DE);
}

void FunctionWrapper::DumpVariableUsages(clang::DiagnosticsEngine & DE) const {
    clang::FunctionDecl const * F = GetFunctionDecl();
    ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
    Analysis.DebugReferenced(DE);
}

void FunctionWrapper::CheckPseudoConstness(PseudoConstnessAnalysisState & State) const {
    clang::FunctionDecl const * F = GetFunctionDecl();
    ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));

    boost::for_each(GetArguments(),
        boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
    boost::for_each(GetLocals(),
        boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
    boost::for_each(GetMembers(),
        boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
}

// Implement wrapper for simple C functions.
class FunctionDeclWrapper : public FunctionWrapper {
public:
    FunctionDeclWrapper(clang::FunctionDecl const * const F)
        : FunctionWrapper()
        , Function(F)
    { }

protected:
    static clang::VarDecl const * CastVarDecl(clang::Decl const * const Decl) {
        clang::VarDecl const * const Candidate =
            clang::dyn_cast<clang::VarDecl const>(Decl);

        return
            (   (0 != Candidate)
            &&  (0 == clang::dyn_cast<clang::ParmVarDecl const>(Candidate))
            )
                ? Candidate
                : 0;
    }

    clang::FunctionDecl const * GetFunctionDecl() const {
        return Function;
    }

    VarDeclSet GetArguments() const {
        VarDeclSet Result;
        boost::copy(
            boost::make_iterator_range(Function->param_begin(), Function->param_end()),
            std::insert_iterator<VarDeclSet>(Result, Result.begin()));
        return Result;
    }

    VarDeclSet GetLocals() const {
        VarDeclSet Result;
        boost::transform(
              boost::make_iterator_range(Function->decls_begin(), Function->decls_end())
            , std::insert_iterator<VarDeclSet>(Result, Result.begin())
            , &FunctionDeclWrapper::CastVarDecl
        );
        Result.erase(0);
        return Result;
    }

    VarDeclSet GetMembers() const {
        return VarDeclSet();
    }

private:
    clang::FunctionDecl const * const Function;
};


// This class collect function declarations and create wrapped classes around them.
class FunctionCollector : public clang::RecursiveASTVisitor<FunctionCollector> {
public:
    void DumpFuncionDeclaration(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpFuncionDeclaration, _1, boost::ref(DE)));
    }

    void DumpArguments(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpArguments, _1, boost::ref(DE)));
    }

    void DumpLocalVariables(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpLocalVariables, _1, boost::ref(DE)));
    }

    void DumpMemberVariables(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpMemberVariables, _1, boost::ref(DE)));
    }

    void DumpVariableChanges(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpVariableChanges, _1, boost::ref(DE)));
    }

    void DumpVariableUsages(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::DumpVariableUsages, _1, boost::ref(DE)));
    }

    void DumpPseudoConstness(clang::DiagnosticsEngine & DE) const {
        PseudoConstnessAnalysisState State;
        boost::for_each(Functions | boost::adaptors::map_values,
            boost::bind(&FunctionWrapper::CheckPseudoConstness, _1, boost::ref(State)));
        State.GenerateReports(DE);
    }

    // this is for visitor pattern
    bool VisitFunctionDecl(clang::FunctionDecl const * F) {
        clang::FunctionDecl const * const CD = F->getCanonicalDecl();
        if (F->isThisDeclarationADefinition()) {
            if (Functions.end() == Functions.find(CD)) {
                Functions[CD] = FunctionWrapperPtr(new FunctionDeclWrapper(F));
            }
        }
        return true;
    }

private:
    typedef boost::shared_ptr<FunctionWrapper> FunctionWrapperPtr;
    typedef std::map<clang::FunctionDecl const *, FunctionWrapperPtr> FunctionWrapperPtrs;

    FunctionWrapperPtrs Functions;
};

} // namespace anonymous


ModuleAnalysis::ModuleAnalysis(clang::CompilerInstance const & Compiler, Target T)
    : boost::noncopyable()
    , clang::ASTConsumer()
    , Reporter(Compiler.getDiagnostics())
    , State(T)
{ }

void ModuleAnalysis::HandleTranslationUnit(clang::ASTContext & Ctx) {
    FunctionCollector Collector;
    Collector.TraverseDecl(Ctx.getTranslationUnitDecl());

    switch (State) {
    case FuncionDeclaration :
        Collector.DumpFuncionDeclaration(Reporter);
        break;
    case Arguments :
        Collector.DumpArguments(Reporter);
        break;
    case LocalVariables :
        Collector.DumpLocalVariables(Reporter);
        break;
    case MemberVariables :
        Collector.DumpMemberVariables(Reporter);
        break;
    case VariableChanges :
        Collector.DumpVariableChanges(Reporter);
        break;
    case VariableUsages :
        Collector.DumpVariableUsages(Reporter);
        break;
    case PseudoConstness :
        Collector.DumpPseudoConstness(Reporter);
        break;
    }
}
