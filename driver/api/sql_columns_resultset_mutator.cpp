#include "driver/api/sql_columns_resultset_mutator.h"
#include <algorithm>  // for std::min

// Column position numbers as described in
// https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlcolumns-function
static const size_t COL_TABLE_CAT = 0;
static const size_t COL_TABLE_SCHEM = 1;
static const size_t COL_TABLE_NAME = 2;
static const size_t COL_COLUMN_NAME = 3;
static const size_t COL_DATA_TYPE = 4;
static const size_t COL_TYPE_NAME = 5;
static const size_t COL_COLUMN_SIZE = 6;
static const size_t COL_BUFFER_LENGTH = 7;
static const size_t COL_DECIMAL_DIGITS = 8;
static const size_t COL_NUM_PREC_RADIX = 9;
static const size_t COL_NULLABLE = 10;
static const size_t COL_REMARKS = 11;
static const size_t COL_COLUMN_DEF = 12;
static const size_t COL_SQL_DATA_TYPE = 13;
static const size_t COL_SQL_DATETIME_SUB = 14;
static const size_t COL_CHAR_OCTET_LENGTH = 15;
static const size_t COL_ORDINAL_POSITION = 16;
static const size_t COL_IS_NULLABLE = 17;

static ColumnInfo parseColumnType(const std::string& type_name)
{
    TypeParser parser{type_name};
    TypeAst ast;
    ColumnInfo ret;
    if (parser.parse(&ast)) {
        ret.assignTypeInfo(ast, Poco::Timezone::name());
        auto type_id = convertUnparametrizedTypeNameToTypeId(ret.type_without_parameters);
        if (type_id == DataSourceTypeId::Unknown) {
            ret.type_without_parameters = "String";
        }
    }
    else {
        ret.type_without_parameters = "String";
    }

    return ret;
}

void SQLColumnsResultSetMutator::transformRow(const std::vector<ColumnInfo> & /*unused*/, Row & row)
{
    using enum DataSourceTypeId;
    // FIXME(slabko): if this get fails, then something is terribly wrong - throw a proper exception
    const auto & type_name_wrapper = std::get<DataSourceType<DataSourceTypeId::String>>(row.fields.at(COL_TYPE_NAME).data);
    const auto & type_name = type_name_wrapper.value;
    const auto column_info = parseColumnType(type_name);
    const TypeInfo & type_info = statement.getTypeInfo(column_info.type, column_info.type_without_parameters);
      // MS Access compatibility fixes
    SQLSMALLINT access_data_type = type_info.data_type;
    std::string access_type_name = type_info.type_name;    // Fix String types for MS Access compatibility with Long Text support
    if (type_info.type_id == String || type_info.type_id == FixedString || type_info.type_id == Array) {
        // Determine whether to use VARCHAR (Short Text) or LONGVARCHAR (Long Text)
        // based on the estimated column size
        int estimated_size = 255; // Default for String/Array types
        
        if (type_info.type_id == FixedString) {
            estimated_size = column_info.fixed_size;
        } else if (type_info.type_id == String || type_info.type_id == Array) {
            // For dynamic strings, check if we have size information
            // Use LONGVARCHAR for potentially large text content
            estimated_size = (type_info.column_size > 255) ? type_info.column_size : 255;
        }

        // Determine if Unicode support is needed
        // Check if the column contains non-ASCII characters or if Unicode is explicitly required
        bool needs_unicode = false;
        
        // Check if the type name indicates Unicode support (e.g., contains UTF-8, Unicode markers)
        if (type_name.find("UTF") != std::string::npos || 
            type_name.find("Unicode") != std::string::npos ||
            type_name.find("NCHAR") != std::string::npos ||
            type_name.find("NVARCHAR") != std::string::npos) {
            needs_unicode = true;
        }
        
        // For ClickHouse, assume Unicode support is generally needed since it uses UTF-8 by default
        // This can be controlled by a driver setting if needed
        // For now, we'll default to Unicode support for better compatibility
        needs_unicode = true;

        // Select appropriate data type based on size and Unicode requirements
        if (needs_unicode) {
            if (estimated_size > 255) {
                access_data_type = SQL_WLONGVARCHAR;
                access_type_name = "WLONGVARCHAR";
            } else {
                access_data_type = SQL_WVARCHAR;
                access_type_name = "WVARCHAR";
            }
        } else {
            // Use standard ANSI types for non-Unicode text
            if (estimated_size > 255) {
                access_data_type = SQL_LONGVARCHAR;
                access_type_name = "LONGVARCHAR";
            } else {
                access_data_type = SQL_VARCHAR;
                access_type_name = "VARCHAR";
            }
        }
    }
    
    row.fields.at(COL_DATA_TYPE).data = DataSourceType<DataSourceTypeId::Int16>{access_data_type};
    row.fields.at(COL_TYPE_NAME).data = DataSourceType<DataSourceTypeId::String>{access_type_name};

    int column_size{};
    switch (type_info.type_id) {
        case Decimal:
            column_size = column_info.precision;
            break;
        case FixedString:
            column_size = column_info.fixed_size;
            break;        case String:
        case Array:
            // Intelligent sizing for MS Access compatibility
            if (access_data_type == SQL_LONGVARCHAR || access_data_type == SQL_WLONGVARCHAR) {
                // For Long Text fields, use a large but reasonable size
                // MS Access Long Text can handle up to 1GB, but we'll use 65535 for ODBC compatibility
                column_size = 65535;
            } else {
                // Standard VARCHAR/WVARCHAR size for Short Text fields
                column_size = 255;
            }
            break;
        default:
            column_size = type_info.column_size;
    }

    row.fields.at(COL_COLUMN_SIZE).data = DataSourceType<DataSourceTypeId::Int32>{column_size};

    if (type_info.num_prec_radix.has_value())
        row.fields.at(COL_NUM_PREC_RADIX).data = DataSourceType<DataSourceTypeId::Int16>{*type_info.num_prec_radix};

    // MS Access expects standard ODBC NULLABLE values
    row.fields.at(COL_NULLABLE).data = DataSourceType<DataSourceTypeId::Int16>{column_info.is_nullable ? SQL_NULLABLE : SQL_NO_NULLS};

    if (type_info.type_id == Decimal)
        row.fields.at(COL_DECIMAL_DIGITS).data = DataSourceType<DataSourceTypeId::Int16>{column_info.scale};
    if (type_info.type_id == DateTime)
        row.fields.at(COL_DECIMAL_DIGITS).data = DataSourceType<DataSourceTypeId::Int16>{0};
    if (type_info.type_id == DateTime64)
        row.fields.at(COL_DECIMAL_DIGITS).data = DataSourceType<DataSourceTypeId::Int16>{column_info.precision};

    row.fields.at(COL_SQL_DATA_TYPE).data = DataSourceType<DataSourceTypeId::Int16>{type_info.sql_data_type};

    if (type_info.sql_datetime_sub.has_value())
        row.fields.at(COL_SQL_DATETIME_SUB).data = DataSourceType<DataSourceTypeId::Int16>{*type_info.sql_datetime_sub};    // MS Access compatible octet length for string types
    // For UTF character encoding considerations, CHAR_OCTET_LENGTH should represent
    // the maximum number of bytes needed to store a character in the column's encoding
    int octet_length = type_info.octet_length;
    if (type_info.type_id == String || type_info.type_id == Array) {
        // For variable-length string types, calculate octet length considering encoding
        switch (access_data_type) {
            case SQL_WLONGVARCHAR:
                // For Unicode Long Text fields (wide characters)
                // In UTF-16, characters can be 2-4 bytes, so we use 4 * column_size as maximum
                octet_length = std::min(column_size * 4, 262140); // 4 bytes per char, MS Access limit
                break;
            case SQL_WVARCHAR:
                // For Unicode VARCHAR fields (wide characters)
                octet_length = std::min(column_size * 4, 1020); // 4 bytes per char, VARCHAR practical limit
                break;
            case SQL_LONGVARCHAR:
                // For ANSI Long Text fields
                // In UTF-8, characters can be 1-4 bytes, so we use 4 * column_size as maximum
                octet_length = std::min(column_size * 4, 262140); // 4 bytes per char, MS Access limit
                break;
            case SQL_VARCHAR:
            default:
                // For ANSI VARCHAR fields
                octet_length = std::min(column_size * 4, 1020); // 4 bytes per char, VARCHAR practical limit
                break;
        }
    } else if (type_info.type_id == FixedString) {
        // For fixed-length strings, the size is already defined
        octet_length = column_info.fixed_size;
    }
    
    row.fields.at(COL_CHAR_OCTET_LENGTH).data = DataSourceType<DataSourceTypeId::Int32>{octet_length};

    row.fields.at(COL_IS_NULLABLE).data = DataSourceType<DataSourceTypeId::String>{column_info.is_nullable ? "YES" : "NO"};
}
