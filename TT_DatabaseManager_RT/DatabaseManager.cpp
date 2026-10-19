#include "DatabaseManager.h"

#if defined(_WIN32)
#include <io.h>
#endif
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#include "CKAll.h"

struct ArrayHeader
{
    int columnCount;
    int rowCount;
    int keyColumn;
};

static inline unsigned char rotl8(unsigned char val, int shift)
{
    shift &= 7;
    return (val << shift) | (val >> (8 - shift));
}

static inline unsigned char rotr8(unsigned char val, int shift)
{
    shift &= 7;
    return (val >> shift) | (val << (8 - shift));
}

static bool ReadInt32At(const char *buffer, int bufferSize, int offset, int *outValue)
{
    if (!buffer || !outValue || offset < 0 || offset > bufferSize - (int)sizeof(int))
        return false;

    memcpy(outValue, buffer + offset, sizeof(int));
    return true;
}

static bool ReadFloatAt(const char *buffer, int bufferSize, int offset, float *outValue)
{
    if (!buffer || !outValue || offset < 0 || offset > bufferSize - (int)sizeof(float))
        return false;

    memcpy(outValue, buffer + offset, sizeof(float));
    return true;
}

static bool FindNullTerminatedSize(const char *buffer, int bufferSize, int offset, int *outSize)
{
    if (!buffer || !outSize || offset < 0 || offset >= bufferSize)
        return false;

    const void *nullPos = memchr(buffer + offset, '\0', (size_t)(bufferSize - offset));
    if (!nullPos)
        return false;

    *outSize = (int)((const char *)nullPos - (buffer + offset)) + 1;
    return true;
}

#if defined(_WIN32)
typedef struct _stat DatabaseFileStat;

static int GetDatabaseFileStat(const char *filename, DatabaseFileStat *buf)
{
    return _stat(filename, buf);
}

static void MakeDatabaseFileWritable(const char *filename, const DatabaseFileStat *)
{
    _chmod(filename, _S_IREAD | _S_IWRITE);
}

static void RestoreDatabaseFilePermissions(const char *filename, const DatabaseFileStat *buf)
{
    bool readable = (buf->st_mode & _S_IREAD) != 0;
    bool writable = (buf->st_mode & _S_IWRITE) != 0;

    if (readable && writable)
        _chmod(filename, _S_IREAD | _S_IWRITE);
    else if (readable)
        _chmod(filename, _S_IREAD);
    else if (writable)
        _chmod(filename, _S_IWRITE);
}
#else
typedef struct stat DatabaseFileStat;

static int GetDatabaseFileStat(const char *filename, DatabaseFileStat *buf)
{
    return stat(filename, buf);
}

static void MakeDatabaseFileWritable(const char *filename, const DatabaseFileStat *buf)
{
    chmod(filename, buf->st_mode | S_IRUSR | S_IWUSR);
}

static void RestoreDatabaseFilePermissions(const char *filename, const DatabaseFileStat *buf)
{
    chmod(filename, buf->st_mode);
}
#endif

DatabaseManager::DatabaseManager(CKContext *context)
    : CKBaseManager(context, TT_DATABASE_MANAGER_GUID, "TT Database Manager"),
      m_Context(context),
      field_2C(false),
      m_ArrayNames(),
      m_Filename(NULL),
      m_Crypted(TRUE)
{
    context->RegisterNewManager(this);
}

DatabaseManager::~DatabaseManager()
{
    Clear();
}

int DatabaseManager::Register(CKSTRING arrayName)
{
    if (!arrayName)
        return 0;

    XArray<CKSTRING>::Iterator it = m_ArrayNames.Begin();
    while (it != m_ArrayNames.End())
        if (!strcmp(*it++, arrayName))
            return 21;

    int nameSize = (int)strlen(arrayName) + 1;
    char *str = new char[(int)strlen(arrayName) + 1];
    strncpy(str, arrayName, nameSize);
    m_ArrayNames.PushBack(str);
    return 1;
}

int DatabaseManager::Clear()
{
    XArray<CKSTRING>::Iterator it = m_ArrayNames.Begin();
    while (it != m_ArrayNames.End())
        if (*it)
            delete[] * it++;

    m_ArrayNames.Clear();

    delete[] m_Filename;
    m_Filename = nullptr;

    return true;
}

int DatabaseManager::Load(CKContext *context, bool autoRegister, CKSTRING arrayName)
{
    int i, c;

    if (!arrayName)
    {
        context->OutputToConsoleExBeep("TT_LoadDatabase: arrayName is NULL");
        return 33;
    }

    if (!m_Filename || m_Filename[0] == '\0')
    {
        context->OutputToConsoleExBeep("TT_LoadDatabase: database filename is NULL (arrayName='%s')", arrayName);
        return 31;
    }

    FILE *fp = fopen(m_Filename, "rb");
    if (!fp)
    {
        context->OutputToConsoleExBeep("TT_LoadDatabase: Error when opening file '%s' (errno=%d)", m_Filename, errno);
        return 31;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 31;
    }

    long fileSizeLong = ftell(fp);
    if (fileSizeLong <= 0 || fileSizeLong > INT_MAX)
    {
        fclose(fp);
        return 33;
    }

    int fileSize = (int)fileSizeLong;
    rewind(fp);

    char *fileData = new char[fileSize];
    if (!fileData)
    {
        fclose(fp);
        return 31;
    }

    size_t readSize = fread(fileData, sizeof(char), fileSize, fp);
    fclose(fp);
    if (readSize != (size_t)fileSize)
    {
        delete[] fileData;
        return 31;
    }

    if (m_Crypted)
    {
        for (i = 0; i < fileSize; ++i)
            fileData[i] = -(rotl8(fileData[i], 3) ^ 0xAF);
    }

    int nameSize = 0;
    int arraySize = 0;
    int chunkSize = 0;
    int pos = 0;
    char *chunk = nullptr;
    while (pos < fileSize)
    {
        if (!FindNullTerminatedSize(fileData, fileSize, pos, &nameSize))
        {
            delete[] fileData;
            return 32;
        }

        if (!ReadInt32At(fileData, fileSize, pos + nameSize, &arraySize))
        {
            delete[] fileData;
            return 32;
        }

        if (arraySize < 0)
        {
            delete[] fileData;
            return 32;
        }

        chunkSize = nameSize + sizeof(int) + arraySize;
        if (chunkSize <= 0 || chunkSize > (fileSize - pos))
        {
            delete[] fileData;
            return 32;
        }

        chunk = &fileData[pos];
        if (strcmp(chunk, arrayName) == 0)
            break;

        pos += chunkSize;
    }
    if (pos >= fileSize || !chunk || strcmp(chunk, arrayName) != 0)
    {
        delete[] fileData;
        return 33;
    }

    CKDataArray *array = (CKDataArray *)context->GetObjectByNameAndClass(chunk, CKCID_DATAARRAY);
    if (!array)
    {
        CK_CREATIONMODE res = CKLOAD_REPLACE;
        array = (CKDataArray *)context->CreateObject(CKCID_DATAARRAY, chunk, CK_OBJECTCREATION_REPLACE, &res);
        context->GetCurrentScene()->AddObjectToScene(array, FALSE);
    }

    array->Clear();
    for (c = array->GetColumnCount() - 1; c >= 0; --c)
        array->RemoveColumn(c);

    if (autoRegister)
        Register(arrayName);

    if (!FindNullTerminatedSize(chunk, fileSize - pos, 0, &nameSize))
    {
        delete[] fileData;
        return 32;
    }
    if (!ReadInt32At(chunk, fileSize - pos, nameSize, &arraySize))
    {
        delete[] fileData;
        return 32;
    }
    if (arraySize < (int)sizeof(ArrayHeader) ||
        nameSize + (int)sizeof(int) > (fileSize - pos) ||
        arraySize > (fileSize - pos) - (nameSize + (int)sizeof(int)))
    {
        delete[] fileData;
        return 32;
    }

    char *arrayData = &chunk[nameSize + sizeof(int)];
    ArrayHeader header;
    memcpy(&header, arrayData, sizeof(ArrayHeader));
    if (header.columnCount < 0 ||
        header.rowCount < 0 ||
        header.keyColumn < -1 ||
        header.keyColumn >= header.columnCount)
    {
        delete[] fileData;
        return 32;
    }

    int offset = sizeof(ArrayHeader);
    for (c = 0; c < header.columnCount; ++c)
    {
        int colNameSize = 0;
        if (!FindNullTerminatedSize(arrayData, arraySize, offset, &colNameSize))
        {
            delete[] fileData;
            return 32;
        }
        if (offset > arraySize - colNameSize - (int)sizeof(CK_ARRAYTYPE))
        {
            delete[] fileData;
            return 32;
        }

        CKSTRING colName = (CKSTRING)&arrayData[offset];
        CK_ARRAYTYPE type = *(CK_ARRAYTYPE *)&arrayData[offset + colNameSize];
        switch (type)
        {
        case CKARRAYTYPE_INT:
            array->InsertColumn(-1, CKARRAYTYPE_INT, colName);
            break;
        case CKARRAYTYPE_FLOAT:
            array->InsertColumn(-1, CKARRAYTYPE_FLOAT, colName);
            break;
        case CKARRAYTYPE_STRING:
            array->InsertColumn(-1, CKARRAYTYPE_STRING, colName);
            break;
        default:
            delete[] fileData;
            return 32;
        }
        offset += colNameSize + sizeof(CK_ARRAYTYPE);
    }

    array->SetKeyColumn(header.keyColumn);

    for (i = 0; i < header.rowCount; ++i)
        array->InsertRow(-1);

    for (c = 0; c < header.columnCount; ++c)
    {
        CK_ARRAYTYPE type = array->GetColumnType(c);
        for (i = 0; i < header.rowCount; ++i)
        {
            switch (type)
            {
            case CKARRAYTYPE_INT:
            {
                int value = 0;
                if (!ReadInt32At(arrayData, arraySize, offset, &value))
                {
                    delete[] fileData;
                    return 32;
                }
                offset += sizeof(int);
                array->SetElementValue(i, c, &value, sizeof(int));
            }
            break;
            case CKARRAYTYPE_FLOAT:
            {
                float value = 0.0f;
                if (!ReadFloatAt(arrayData, arraySize, offset, &value))
                {
                    delete[] fileData;
                    return 32;
                }
                offset += sizeof(float);
                array->SetElementValue(i, c, &value, sizeof(float));
            }
            break;
            case CKARRAYTYPE_STRING:
            {
                int size = 0;
                if (!FindNullTerminatedSize(arrayData, arraySize, offset, &size))
                {
                    delete[] fileData;
                    return 32;
                }
                char *str = (char *)&arrayData[offset];
                char *nstr = new char[size];
                strncpy(nstr, str, size);
                offset += size;
                array->SetElementValue(i, c, nstr, size);
                delete[] nstr;
            }
            break;
            default:
                delete[] fileData;
                return 32;
            }
        }
    }

    if (offset > arraySize)
    {
        delete[] fileData;
        return 32;
    }

    delete[] fileData;
    return 1;
}

int DatabaseManager::Save(CKContext *context)
{
    if (!m_Filename || m_Filename[0] == '\0')
        return 41;

    int n, i, c;
    int fileSize = 0;
    char *fileData = NULL;

    const int arrayNameCount = m_ArrayNames.Size();
    for (n = 0; n < arrayNameCount; ++n)
    {
        CKDataArray *array = (CKDataArray *)context->GetObjectByNameAndClass(m_ArrayNames[n], CKCID_DATAARRAY);
        if (!array)
        {
            delete[] fileData;
            return 42;
        }

        int chunkSize = 0;
        CKSTRING arrayName = array->GetName();
        chunkSize += (arrayName) ? (int)strlen(arrayName) : 0;
        chunkSize += 1 + sizeof(int) + sizeof(ArrayHeader);

        const int columnCount = array->GetColumnCount();
        const int rowCount = array->GetRowCount();

        for (c = 0; c < columnCount; ++c)
        {
            CKSTRING colName = array->GetColumnName(c);
            chunkSize += (colName) ? (int)strlen(colName) : 0;
            chunkSize += 1 + sizeof(int);
        }

        for (c = 0; c < columnCount; ++c)
        {
            CK_ARRAYTYPE type = array->GetColumnType(c);
            switch (type)
            {
            case CKARRAYTYPE_INT:
                chunkSize += rowCount * sizeof(int);
                break;
            case CKARRAYTYPE_FLOAT:
                chunkSize += rowCount * sizeof(float);
                break;
            case CKARRAYTYPE_STRING:
            {
                CKSTRING str = NULL;
                for (i = 0; i < rowCount; ++i)
                {
                    array->GetElementValue(i, c, &str);
                    if (str)
                        chunkSize += (int)strlen(str);
                    chunkSize += 1;
                }
            }
            break;
            default:
                break;
            }
        }

        int offset = 0;
        int nameLength = 0;
        char *chunk = new char[chunkSize];

        arrayName = array->GetName();
        if (arrayName)
        {
            nameLength = (int)strlen(arrayName);
            strncpy(&chunk[offset], arrayName, nameLength + 1);
        }
        else
        {
            nameLength = 0;
            chunk[offset] = '\0';
        }
        offset += nameLength + 1;

        *(int *)&chunk[offset] = chunkSize - (nameLength + 1 + sizeof(int));
        offset += sizeof(int);

        *(int *)&chunk[offset] = columnCount;
        offset += sizeof(int);

        *(int *)&chunk[offset] = rowCount;
        offset += sizeof(int);

        *(int *)&chunk[offset] = array->GetKeyColumn();
        offset += sizeof(int);

        for (c = 0; c < columnCount; ++c)
        {
            CKSTRING colName = array->GetColumnName(c);
            if (colName)
            {
                nameLength = (int)strlen(colName);
                strncpy(&chunk[offset], colName, nameLength + 1);
            }
            else
            {
                nameLength = 0;
                chunk[offset] = '\0';
            }
            offset += nameLength + 1;

            *(CK_ARRAYTYPE *)&chunk[offset] = array->GetColumnType(c);
            offset += sizeof(CK_ARRAYTYPE);
        }

        for (c = 0; c < columnCount; ++c)
        {
            CK_ARRAYTYPE type = array->GetColumnType(c);
            for (i = 0; i < rowCount; ++i)
            {
                switch (type)
                {
                case CKARRAYTYPE_INT:
                {
                    int value = 0;
                    array->GetElementValue(i, c, &value);
                    *(int *)&chunk[offset] = value;
                    offset += sizeof(int);
                }
                break;
                case CKARRAYTYPE_FLOAT:
                {
                    float value = 0;
                    array->GetElementValue(i, c, &value);
                    *(float *)&chunk[offset] = value;
                    offset += sizeof(float);
                }
                break;
                case CKARRAYTYPE_STRING:
                {
                    char *str = NULL;
                    array->GetElementValue(i, c, &str);
                    if (str)
                    {
                        nameLength = (int)strlen(str);
                        strncpy(&chunk[offset], str, nameLength + 1);
                    }
                    else
                    {
                        nameLength = 0;
                        chunk[offset] = '\0';
                    }
                    offset += nameLength + 1;
                }
                break;
                default:
                    break;
                }
            }
        }

        if (fileSize == 0)
        {
            fileData = chunk;
        }
        else
        {
            char *NewFileData = new char[fileSize + chunkSize];
            memcpy(NewFileData, fileData, fileSize);
            memcpy(&NewFileData[fileSize], chunk, chunkSize);
            delete[] fileData;
            delete[] chunk;
            fileData = NewFileData;
        }

        fileSize += chunkSize;
    }

    if (!fileData)
        return 41;

    if (m_Crypted)
    {
        for (i = 0; i < fileSize; ++i)
            fileData[i] = rotr8(-fileData[i] ^ 0xAF, 3);
    }

    DatabaseFileStat buf;
    int statRes = GetDatabaseFileStat(m_Filename, &buf);
    if (statRes == 0)
        MakeDatabaseFileWritable(m_Filename, &buf);

    FILE *fp = fopen(m_Filename, "wb");
    if (!fp)
    {
        if (statRes == 0)
            RestoreDatabaseFilePermissions(m_Filename, &buf);
        delete[] fileData;
        return 41;
    }

    fwrite(fileData, fileSize, sizeof(char), fp);
    fclose(fp);

    if (statRes == 0)
        RestoreDatabaseFilePermissions(m_Filename, &buf);

    delete[] fileData;
    return 1;
}

bool DatabaseManager::SetProperty(CKSTRING filename, CKBOOL crypted)
{
    if (!filename || filename[0] == '\0')
        return false;

    // Sandboxed or bundled players can provide a writable database directory.
    // Game compositions commonly embed an absolute path based on their asset
    // root, so merely changing the process working directory is insufficient.
    // Preserve the requested basename while redirecting both reads and writes.
    const char *storedFilename = filename;
    XString redirectedFilename;
    const char *databaseDirectory = getenv("TT_DATABASE_DIRECTORY");
    if (databaseDirectory && databaseDirectory[0] != '\0')
    {
        const char *basename = filename;
        for (const char *cursor = filename; *cursor; ++cursor)
        {
            if (*cursor == '/' || *cursor == '\\')
                basename = cursor + 1;
        }

        if (basename[0] != '\0')
        {
            redirectedFilename = databaseDirectory;
            const int directoryLength = redirectedFilename.Length();
            if (directoryLength > 0)
            {
                const char last = redirectedFilename[directoryLength - 1];
                if (last != '/' && last != '\\')
                    redirectedFilename << "/";
            }
            redirectedFilename << basename;
            storedFilename = redirectedFilename.CStr();
        }
    }

    // Copy the filename so we own it independent of behavior parameter lifetime.
    delete[] m_Filename;
    int len = (int)strlen(storedFilename) + 1;
    m_Filename = new char[len];
    strncpy(m_Filename, storedFilename, len);

    if (m_Context)
        m_Context->OutputToConsoleEx("TT Database file: %s", m_Filename);

    m_Crypted = crypted;
    return true;
}
