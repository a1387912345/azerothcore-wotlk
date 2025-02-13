/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 */

#ifndef QUERYRESULT_H
#define QUERYRESULT_H

#include "Errors.h"
#include "Field.h"
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80001
typedef bool my_bool;
#endif

class ResultSet
{
public:
    ResultSet(MYSQL_RES* result, MYSQL_FIELD* fields, uint64 rowCount, uint32 fieldCount);
    ~ResultSet();

    bool NextRow();
    [[nodiscard]] uint64 GetRowCount() const { return _rowCount; }
    [[nodiscard]] uint32 GetFieldCount() const { return _fieldCount; }
#ifdef ELUNA
    std::string GetFieldName(uint32 index) const;
#endif
    [[nodiscard]] Field* Fetch() const { return _currentRow; }
    const Field& operator [] (uint32 index) const
    {
        ASSERT(index < _fieldCount);
        return _currentRow[index];
    }

protected:
    uint64 _rowCount;
    Field* _currentRow;
    uint32 _fieldCount;

private:
    void CleanUp();
    MYSQL_RES* _result;
    MYSQL_FIELD* _fields;
};

typedef std::shared_ptr<ResultSet> QueryResult;

class PreparedResultSet
{
public:
    PreparedResultSet(MYSQL_STMT* stmt, MYSQL_RES* result, uint64 rowCount, uint32 fieldCount);
    ~PreparedResultSet();

    bool NextRow();
    [[nodiscard]] uint64 GetRowCount() const { return m_rowCount; }
    [[nodiscard]] uint32 GetFieldCount() const { return m_fieldCount; }

    [[nodiscard]] Field* Fetch() const
    {
        ASSERT(m_rowPosition < m_rowCount);
        return m_rows[uint32(m_rowPosition)];
    }

    const Field& operator [] (uint32 index) const
    {
        ASSERT(m_rowPosition < m_rowCount);
        ASSERT(index < m_fieldCount);
        return m_rows[uint32(m_rowPosition)][index];
    }

protected:
    std::vector<Field*> m_rows;
    uint64 m_rowCount;
    uint64 m_rowPosition;
    uint32 m_fieldCount;

private:
    MYSQL_BIND* m_rBind;
    MYSQL_STMT* m_stmt;
    MYSQL_RES* m_res;

    my_bool* m_isNull;
    unsigned long* m_length;

    void FreeBindBuffer();
    void CleanUp();
    bool _NextRow();
};

typedef std::shared_ptr<PreparedResultSet> PreparedQueryResult;

#endif
