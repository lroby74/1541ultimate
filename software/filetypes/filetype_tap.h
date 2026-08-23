#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "filetypes.h"
#include "browsable_root.h"
#include "indexed_list.h"

// Nessuna gestione dei file .idx: l'elenco dei programmi dentro un nastro e le
// voci "From Here" sono state tolte di proposito.  Il punto del nastro
// si sceglie col contanastro, dal menu del 1530 (GO TO).
class FileTypeTap : public FileType
{
	BrowsableDirEntry *node;
public:
    FileTypeTap(BrowsableDirEntry *par);
    ~FileTypeTap();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
    static SubsysResultCode_e execute_st(SubsysCommand *cmd);
};

#endif
