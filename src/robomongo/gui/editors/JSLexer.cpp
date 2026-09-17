#include "robomongo/gui/editors/JSLexer.h"

#include <Qsci/qscilexerjavascript.h>

namespace Robomongo
{
    JSLexer::JSLexer(QObject *parent) : QsciLexerJavaScript(parent)
    {
    }

    QColor JSLexer::defaultPaper(int) const
    {
        return QColor("#ffffff");
    }

    QColor JSLexer::defaultColor(int style) const
    {
        switch (style)
        {
        case Default:
            return QColor("#444444");

        case Comment:
        case CommentLine:
        case CommentDoc:
        case CommentLineDoc:
            return QColor("#707070");

        case Number:
            return QColor("#8b6e51");

        case Keyword:
            return QColor("#736684");

        case DoubleQuotedString:
        case SingleQuotedString:
        case RawString:
            return QColor("#557c69");

        case PreProcessor:
            return QColor("#666666");

        case Operator:
            return QColor("#626262");

        case Regex:
            return QColor("#896b7b");

        case CommentDocKeyword:
            return QColor("#666666");

        case UnclosedString:
        case CommentDocKeywordError:
            return QColor("#a6535c");

        case InactiveDefault:
        case InactiveUUID:
        case InactiveCommentLineDoc:
        case InactiveKeywordSet2:
        case InactiveCommentDocKeyword:
        case InactiveCommentDocKeywordError:
        case InactiveComment:
        case InactiveCommentLine:
        case InactiveNumber:
        case InactiveCommentDoc:
        case InactiveKeyword:
        case InactiveDoubleQuotedString:
        case InactiveSingleQuotedString:
        case InactiveRawString:
        case InactivePreProcessor:
        case InactiveOperator:
        case InactiveIdentifier:
        case InactiveGlobalClass:
        case InactiveUnclosedString:
        case InactiveVerbatimString:
        case InactiveRegex:
            return QColor("#818181");
        }

        return QColor("#444444");
    }

    const char *JSLexer::keywords(int set) const
    {
        if (set == 1)
            return
                "abstract boolean break byte case catch char class const continue "
                "debugger default delete do double else enum export extends final "
                "finally float for function goto if implements import in instanceof "
                "int interface long native new package private protected public "
                "return short static super switch synchronized this throw throws "
                "transient try typeof var void volatile while with "
                "ISODate ObjectId Mongo Date NumberInt Number NumberLong Timestamp _id null false true "
                "UUID LUUID PYUUID CSUUID JUUID NUUID ";

        return 0;
    }
}
