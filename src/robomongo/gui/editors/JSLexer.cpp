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
            return QColor("#243247");

        case Comment:
        case CommentLine:
        case CommentDoc:
        case CommentLineDoc:
            return QColor("#748398");

        case Number:
            return QColor("#b15b20");

        case Keyword:
            return QColor("#6e50ad");

        case DoubleQuotedString:
        case SingleQuotedString:
        case RawString:
            return QColor("#247c68");

        case PreProcessor:
            return QColor("#356fa8");

        case Operator:
            return QColor("#50657e");

        case Regex:
            return QColor("#a04877");

        case CommentDocKeyword:
            return QColor("#356fa8");

        case UnclosedString:
        case CommentDocKeywordError:
            return QColor("#bd4052");

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
            return QColor("#8794a7");
        }

        return QColor("#243247");
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
