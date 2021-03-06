/*  Copyright (C) 2012-2014  László Nagy
    This file is part of Constantine.

    Constantine implements pseudo const analysis.

    Constantine is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Constantine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>

enum Target
    { FuncionDeclaration
    , VariableDeclaration
    , VariableChanges
    , VariableUsages
    , PseudoConstness
    };

// It runs the pseudo const analysis on the given translation unit.
class ModuleAnalysis : public clang::ASTConsumer {
public:
    ModuleAnalysis(clang::CompilerInstance const &, Target);

    void HandleTranslationUnit(clang::ASTContext &);


    ModuleAnalysis(ModuleAnalysis const &) = delete;
    ModuleAnalysis & operator=(ModuleAnalysis const &) = delete;

private:
    clang::DiagnosticsEngine & Reporter;
    Target const State;
};
