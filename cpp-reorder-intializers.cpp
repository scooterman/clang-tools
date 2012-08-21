/*
    Copyright 2011 Victor Vicente de Carvalho victor (dot) v (dot) carvalho @ gmail (dot) com

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <string>
#include <iostream>
#include <fstream>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommandLineClangTool.h>
#include <clang/Rewrite/Rewriter.h>

using namespace clang;
using namespace std;
using namespace clang::tooling;

class ReorderingFieldsASTVisitor : public RecursiveASTVisitor<ReorderingFieldsASTVisitor> {
public:
    ReorderingFieldsASTVisitor ( Rewriter &R )
        : rewriter ( R ) {
    }

    std::string getStringForRange ( SourceRange range ) {
        std::string returnStr;

        int length = rewriter.getRangeSize ( range );

        if ( length >= 0 ) {
            bool invalid = false;
            const char* declaration = rewriter.getSourceMgr().getCharacterData ( range.getBegin(), &invalid );

            if ( !invalid ) {
                returnStr.assign ( declaration, length );
            } else {
                llvm::errs() << "Failed to recover the class declaration at: ";
                range.getBegin().print ( llvm::outs(), rewriter.getSourceMgr() );
            }
        }

        return returnStr;
    }

    virtual bool VisitCXXConstructorDecl ( CXXConstructorDecl* d ) {
        if ( rewriter.getSourceMgr().getFileID ( d->getLocation() ) != mainFileID ) {
            return true;
        }

        if ( d->getNumCtorInitializers() ) {
            std::string reorderedInitializedString;
            const auto& end = d->init_end();

            SourceLocation begin;
            bool first = true;

            for ( auto it = d->init_begin() ; it != end; ++it ) {
                if ( ! ( *it )->isWritten() ) {
                    continue;
                }

                if ( ( *it )->getSourceLocation() < begin || begin.isInvalid() ) {
                    begin = ( *it )->getSourceLocation();
                }

                reorderedInitializedString += ( first ? "" : ",\n" ) + getStringForRange ( ( *it )->getSourceRange() );
                first = false;
            }

            if ( !begin.isValid() ) {
                return true;
            }

            SourceRange rng ( begin, d->getBody()->getSourceRange().getBegin().getLocWithOffset ( -1 ) );


            rewriter.ReplaceText ( rng , reorderedInitializedString );
        }

        return true;
    }

    void setMainFileID ( FileID file ) {
        mainFileID = file;
    }

private:
    Rewriter &rewriter;
    FileID mainFileID;
};

class ReorderingFieldsConsumer : public ASTConsumer {
public:
    ReorderingFieldsConsumer ( Rewriter &R )
        : Visitor ( R ) {
    }

    virtual void HandleTranslationUnit ( clang::ASTContext &Context ) {
        Visitor.TraverseDecl ( Context.getTranslationUnitDecl() );
    }

    virtual ~ReorderingFieldsConsumer() {
    }

    void setMainFileID ( FileID file ) {
        Visitor.setMainFileID ( file );
    }

private:
    ReorderingFieldsASTVisitor Visitor;
};

class ReorderFieldsAction : public ASTFrontendAction {
public:
    virtual clang::ASTConsumer *CreateASTConsumer (
        clang::CompilerInstance &Compiler, llvm::StringRef InFile ) {

        rewriter.setSourceMgr ( Compiler.getSourceManager(), Compiler.getLangOpts() );
        tool = new ReorderingFieldsConsumer ( rewriter );
        return tool;
    }

    virtual void ExecuteAction() {
        tool->setMainFileID ( rewriter.getSourceMgr().getMainFileID() );
        ASTFrontendAction::ExecuteAction();
    }

    virtual void EndSourceFileAction() {
        ASTFrontendAction::EndSourceFileAction();
        const RewriteBuffer *RewriteBuf = rewriter.getRewriteBufferFor ( rewriter.getSourceMgr().getMainFileID() );

        if ( RewriteBuf != NULL ) {
            const FileEntry* entry = rewriter.getSourceMgr().getFileEntryForID ( rewriter.getSourceMgr().getMainFileID() );

            std::string error;
            llvm::raw_fd_ostream file ( entry->getName(), error );

            RewriteBuf->write ( file );

            file.close();
        }
    }

    Rewriter rewriter;
    ReorderingFieldsConsumer* tool;
};

int main ( int argc, const char** argv ) {
    CommandLineClangTool tool;
    tool.initialize ( argc, argv );

    return tool.run ( newFrontendActionFactory<ReorderFieldsAction>() );
}
