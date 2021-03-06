/*
 * Implementation of the Microsoft Installer (msi.dll)
 *
 * Copyright 2005 Aric Stewart for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS
#define NONAMELESSUNION

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "shlwapi.h"
#include "wine/debug.h"
#include "msi.h"
#include "msiquery.h"
#include "msipriv.h"
#include "wincrypt.h"
#include "winver.h"
#include "winuser.h"
#include "wine/unicode.h"
#include "sddl.h"

WINE_DEFAULT_DEBUG_CHANNEL(msi);

/*
 * These apis are defined in MSI 3.0
 */

typedef struct tagMediaInfo
{
    struct list entry;
    LPWSTR  path;
    WCHAR   szIndex[10];
    DWORD   index;
} media_info;

static UINT OpenSourceKey(LPCWSTR szProduct, HKEY* key, DWORD dwOptions,
                          MSIINSTALLCONTEXT context, BOOL create)
{
    HKEY rootkey = 0; 
    UINT rc = ERROR_FUNCTION_FAILED;
    static const WCHAR szSourceList[] = {'S','o','u','r','c','e','L','i','s','t',0};

    if (context == MSIINSTALLCONTEXT_USERUNMANAGED)
    {
        if (dwOptions & MSICODE_PATCH)
            rc = MSIREG_OpenUserPatchesKey(szProduct, &rootkey, create);
        else
            rc = MSIREG_OpenUserProductsKey(szProduct, &rootkey, create);
    }
    else if (context == MSIINSTALLCONTEXT_USERMANAGED)
    {
        if (dwOptions & MSICODE_PATCH)
            rc = MSIREG_OpenUserPatchesKey(szProduct, &rootkey, create);
        else
            rc = MSIREG_OpenLocalManagedProductKey(szProduct, &rootkey, create);
    }
    else if (context == MSIINSTALLCONTEXT_MACHINE)
    {
        if (dwOptions & MSICODE_PATCH)
            rc = MSIREG_OpenPatchesKey(szProduct, &rootkey, create);
        else
            rc = MSIREG_OpenLocalClassesProductKey(szProduct, &rootkey, create);
    }

    if (rc != ERROR_SUCCESS)
    {
        if (dwOptions & MSICODE_PATCH)
            return ERROR_UNKNOWN_PATCH;
        else
            return ERROR_UNKNOWN_PRODUCT;
    }

    if (create)
        rc = RegCreateKeyW(rootkey, szSourceList, key);
    else
    {
        rc = RegOpenKeyW(rootkey,szSourceList, key);
        if (rc != ERROR_SUCCESS)
            rc = ERROR_BAD_CONFIGURATION;
    }

    return rc;
}

static UINT OpenMediaSubkey(HKEY rootkey, HKEY *key, BOOL create)
{
    UINT rc;
    static const WCHAR media[] = {'M','e','d','i','a',0};

    if (create)
        rc = RegCreateKeyW(rootkey, media, key);
    else
        rc = RegOpenKeyW(rootkey,media, key); 

    return rc;
}

static UINT OpenNetworkSubkey(HKEY rootkey, HKEY *key, BOOL create)
{
    UINT rc;
    static const WCHAR net[] = {'N','e','t',0};

    if (create)
        rc = RegCreateKeyW(rootkey, net, key);
    else
        rc = RegOpenKeyW(rootkey, net, key); 

    return rc;
}

static UINT OpenURLSubkey(HKEY rootkey, HKEY *key, BOOL create)
{
    UINT rc;
    static const WCHAR URL[] = {'U','R','L',0};

    if (create)
        rc = RegCreateKeyW(rootkey, URL, key);
    else
        rc = RegOpenKeyW(rootkey, URL, key); 

    return rc;
}

/******************************************************************
 *  MsiSourceListEnumMediaDisksA   (MSI.@)
 */
UINT WINAPI MsiSourceListEnumMediaDisksA(LPCSTR szProductCodeOrPatchCode,
                                         LPCSTR szUserSid, MSIINSTALLCONTEXT dwContext,
                                         DWORD dwOptions, DWORD dwIndex, LPDWORD pdwDiskId,
                                         LPSTR szVolumeLabel, LPDWORD pcchVolumeLabel,
                                         LPSTR szDiskPrompt, LPDWORD pcchDiskPrompt)
{
    LPWSTR product = NULL;
    LPWSTR usersid = NULL;
    LPWSTR volume = NULL;
    LPWSTR prompt = NULL;
    UINT r = ERROR_INVALID_PARAMETER;

    TRACE("(%s, %s, %d, %d, %d, %p, %p, %p, %p, %p)\n", debugstr_a(szProductCodeOrPatchCode),
          debugstr_a(szUserSid), dwContext, dwOptions, dwIndex, pdwDiskId,
          szVolumeLabel, pcchVolumeLabel, szDiskPrompt, pcchDiskPrompt);

    if (szDiskPrompt && !pcchDiskPrompt)
        return ERROR_INVALID_PARAMETER;

    if (szProductCodeOrPatchCode) product = strdupAtoW(szProductCodeOrPatchCode);
    if (szUserSid) usersid = strdupAtoW(szUserSid);

    /* FIXME: add tests for an invalid format */

    if (pcchVolumeLabel)
        volume = msi_alloc(*pcchVolumeLabel * sizeof(WCHAR));

    if (pcchDiskPrompt)
        prompt = msi_alloc(*pcchDiskPrompt * sizeof(WCHAR));

    if (volume) *volume = '\0';
    if (prompt) *prompt = '\0';
    r = MsiSourceListEnumMediaDisksW(product, usersid, dwContext, dwOptions,
                                     dwIndex, pdwDiskId, volume, pcchVolumeLabel,
                                     prompt, pcchDiskPrompt);
    if (r != ERROR_SUCCESS)
        goto done;

    if (szVolumeLabel && pcchVolumeLabel)
        WideCharToMultiByte(CP_ACP, 0, volume, -1, szVolumeLabel,
                            *pcchVolumeLabel + 1, NULL, NULL);

    if (szDiskPrompt)
        WideCharToMultiByte(CP_ACP, 0, prompt, -1, szDiskPrompt,
                            *pcchDiskPrompt + 1, NULL, NULL);

done:
    msi_free(product);
    msi_free(usersid);
    msi_free(volume);
    msi_free(prompt);

    return r;
}

/******************************************************************
 *  MsiSourceListEnumMediaDisksW   (MSI.@)
 */
UINT WINAPI MsiSourceListEnumMediaDisksW(LPCWSTR szProductCodeOrPatchCode,
                                         LPCWSTR szUserSid, MSIINSTALLCONTEXT dwContext,
                                         DWORD dwOptions, DWORD dwIndex, LPDWORD pdwDiskId,
                                         LPWSTR szVolumeLabel, LPDWORD pcchVolumeLabel,
                                         LPWSTR szDiskPrompt, LPDWORD pcchDiskPrompt)
{
    WCHAR squished_pc[GUID_SIZE];
    LPWSTR value = NULL;
    LPWSTR data = NULL;
    LPWSTR ptr;
    HKEY source, media;
    DWORD valuesz, datasz = 0;
    DWORD type;
    DWORD numvals, size;
    LONG res;
    UINT r;
    static int index = 0;

    TRACE("(%s, %s, %d, %d, %d, %p, %p, %p, %p)\n", debugstr_w(szProductCodeOrPatchCode),
          debugstr_w(szUserSid), dwContext, dwOptions, dwIndex, szVolumeLabel,
          pcchVolumeLabel, szDiskPrompt, pcchDiskPrompt);

    if (!szProductCodeOrPatchCode ||
        !squash_guid(szProductCodeOrPatchCode, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (dwContext == MSIINSTALLCONTEXT_MACHINE && szUserSid)
        return ERROR_INVALID_PARAMETER;

    if (dwOptions != MSICODE_PRODUCT && dwOptions != MSICODE_PATCH)
        return ERROR_INVALID_PARAMETER;

    if (szDiskPrompt && !pcchDiskPrompt)
        return ERROR_INVALID_PARAMETER;

    if (dwIndex == 0)
        index = 0;

    if (dwIndex != index)
        return ERROR_INVALID_PARAMETER;

    r = OpenSourceKey(szProductCodeOrPatchCode, &source,
                      dwOptions, dwContext, FALSE);
    if (r != ERROR_SUCCESS)
        return r;

    r = OpenMediaSubkey(source, &media, FALSE);
    if (r != ERROR_SUCCESS)
    {
        RegCloseKey(source);
        return ERROR_NO_MORE_ITEMS;
    }

    if (!pcchVolumeLabel && !pcchDiskPrompt)
    {
        r = RegEnumValueW(media, dwIndex, NULL, NULL, NULL,
                          &type, NULL, NULL);
        goto done;
    }

    res = RegQueryInfoKeyW(media, NULL, NULL, NULL, NULL, NULL,
                           NULL, &numvals, &valuesz, &datasz, NULL, NULL);
    if (res != ERROR_SUCCESS)
    {
        r = ERROR_BAD_CONFIGURATION;
        goto done;
    }

    value = msi_alloc(++valuesz * sizeof(WCHAR));
    data = msi_alloc(++datasz * sizeof(WCHAR));
    if (!value || !data)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }

    r = RegEnumValueW(media, dwIndex, value, &valuesz,
                      NULL, &type, (LPBYTE)data, &datasz);
    if (r != ERROR_SUCCESS)
        goto done;

    if (pdwDiskId)
        *pdwDiskId = atolW(value);

    ptr = strchrW(data, ';');
    if (!ptr)
        ptr = data;
    else
        *ptr = '\0';

    if (pcchVolumeLabel)
    {
        size = lstrlenW(data);
        if (size >= *pcchVolumeLabel)
            r = ERROR_MORE_DATA;
        else if (szVolumeLabel)
            lstrcpyW(szVolumeLabel, data);

        *pcchVolumeLabel = size;
    }

    if (pcchDiskPrompt)
    {
        if (!*ptr)
            ptr++;

        size = lstrlenW(ptr);
        if (size >= *pcchDiskPrompt)
            r = ERROR_MORE_DATA;
        else if (szDiskPrompt)
            lstrcpyW(szDiskPrompt, ptr);

        *pcchDiskPrompt = size;
    }

    index++;

done:
    msi_free(value);
    msi_free(data);
    RegCloseKey(source);

    return r;
}

/******************************************************************
 *  MsiSourceListEnumSourcesA   (MSI.@)
 */
UINT WINAPI MsiSourceListEnumSourcesA(LPCSTR szProductCodeOrPatch, LPCSTR szUserSid,
                                      MSIINSTALLCONTEXT dwContext,
                                      DWORD dwOptions, DWORD dwIndex,
                                      LPSTR szSource, LPDWORD pcchSource)
{
    LPWSTR product = NULL;
    LPWSTR usersid = NULL;
    LPWSTR source = NULL;
    DWORD len = 0;
    UINT r = ERROR_INVALID_PARAMETER;
    static int index = 0;

    TRACE("(%s, %s, %d, %d, %d, %p, %p)\n", debugstr_a(szProductCodeOrPatch),
          debugstr_a(szUserSid), dwContext, dwOptions, dwIndex, szSource, pcchSource);

    if (dwIndex == 0)
        index = 0;

    if (szSource && !pcchSource)
        goto done;

    if (dwIndex != index)
        goto done;

    if (szProductCodeOrPatch) product = strdupAtoW(szProductCodeOrPatch);
    if (szUserSid) usersid = strdupAtoW(szUserSid);

    r = MsiSourceListEnumSourcesW(product, usersid, dwContext, dwOptions,
                                  dwIndex, NULL, &len);
    if (r != ERROR_SUCCESS)
        goto done;

    source = msi_alloc(++len * sizeof(WCHAR));
    if (!source)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }

    *source = '\0';
    r = MsiSourceListEnumSourcesW(product, usersid, dwContext, dwOptions,
                                  dwIndex, source, &len);
    if (r != ERROR_SUCCESS)
        goto done;

    len = WideCharToMultiByte(CP_ACP, 0, source, -1, NULL, 0, NULL, NULL);
    if (pcchSource && *pcchSource >= len)
        WideCharToMultiByte(CP_ACP, 0, source, -1, szSource, len, NULL, NULL);
    else if (szSource)
        r = ERROR_MORE_DATA;

    if (pcchSource)
        *pcchSource = len - 1;

done:
    msi_free(product);
    msi_free(usersid);
    msi_free(source);

    if (r == ERROR_SUCCESS)
    {
        if (szSource || !pcchSource) index++;
    }
    else if (dwIndex > index)
        index = 0;

    return r;
}

/******************************************************************
 *  MsiSourceListEnumSourcesW   (MSI.@)
 */
UINT WINAPI MsiSourceListEnumSourcesW(LPCWSTR szProductCodeOrPatch, LPCWSTR szUserSid,
                                      MSIINSTALLCONTEXT dwContext,
                                      DWORD dwOptions, DWORD dwIndex,
                                      LPWSTR szSource, LPDWORD pcchSource)
{
    WCHAR squished_pc[GUID_SIZE];
    WCHAR name[32];
    HKEY source = NULL;
    HKEY subkey = NULL;
    LONG res;
    UINT r = ERROR_INVALID_PARAMETER;
    static int index = 0;

    static const WCHAR format[] = {'%','d',0};

    TRACE("(%s, %s, %d, %d, %d, %p, %p)\n", debugstr_w(szProductCodeOrPatch),
          debugstr_w(szUserSid), dwContext, dwOptions, dwIndex, szSource, pcchSource);

    if (dwIndex == 0)
        index = 0;

    if (!szProductCodeOrPatch || !squash_guid(szProductCodeOrPatch, squished_pc))
        goto done;

    if (szSource && !pcchSource)
        goto done;

    if (!(dwOptions & (MSISOURCETYPE_NETWORK | MSISOURCETYPE_URL)))
        goto done;

    if ((dwOptions & MSISOURCETYPE_NETWORK) && (dwOptions & MSISOURCETYPE_URL))
        goto done;

    if (dwContext == MSIINSTALLCONTEXT_MACHINE && szUserSid)
        goto done;

    if (dwIndex != index)
        goto done;

    r = OpenSourceKey(szProductCodeOrPatch, &source,
                      dwOptions, dwContext, FALSE);
    if (r != ERROR_SUCCESS)
        goto done;

    if (dwOptions & MSISOURCETYPE_NETWORK)
        r = OpenNetworkSubkey(source, &subkey, FALSE);
    else if (dwOptions & MSISOURCETYPE_URL)
        r = OpenURLSubkey(source, &subkey, FALSE);

    if (r != ERROR_SUCCESS)
    {
        r = ERROR_NO_MORE_ITEMS;
        goto done;
    }

    sprintfW(name, format, dwIndex + 1);

    res = RegQueryValueExW(subkey, name, 0, 0, (LPBYTE)szSource, pcchSource);
    if (res != ERROR_SUCCESS && res != ERROR_MORE_DATA)
        r = ERROR_NO_MORE_ITEMS;

done:
    RegCloseKey(subkey);
    RegCloseKey(source);

    if (r == ERROR_SUCCESS)
    {
        if (szSource || !pcchSource) index++;
    }
    else if (dwIndex > index)
        index = 0;

    return r;
}

/******************************************************************
 *  MsiSourceListGetInfoA   (MSI.@)
 */
UINT WINAPI MsiSourceListGetInfoA( LPCSTR szProduct, LPCSTR szUserSid,
                                   MSIINSTALLCONTEXT dwContext, DWORD dwOptions,
                                   LPCSTR szProperty, LPSTR szValue,
                                   LPDWORD pcchValue)
{
    UINT ret;
    LPWSTR product = NULL;
    LPWSTR usersid = NULL;
    LPWSTR property = NULL;
    LPWSTR value = NULL;
    DWORD len = 0;

    if (szValue && !pcchValue)
        return ERROR_INVALID_PARAMETER;

    if (szProduct) product = strdupAtoW(szProduct);
    if (szUserSid) usersid = strdupAtoW(szUserSid);
    if (szProperty) property = strdupAtoW(szProperty);

    ret = MsiSourceListGetInfoW(product, usersid, dwContext, dwOptions,
                                property, NULL, &len);
    if (ret != ERROR_SUCCESS)
        goto done;

    value = msi_alloc(++len * sizeof(WCHAR));
    if (!value)
        return ERROR_OUTOFMEMORY;

    *value = '\0';
    ret = MsiSourceListGetInfoW(product, usersid, dwContext, dwOptions,
                                property, value, &len);
    if (ret != ERROR_SUCCESS)
        goto done;

    len = WideCharToMultiByte(CP_ACP, 0, value, -1, NULL, 0, NULL, NULL);
    if (*pcchValue >= len)
        WideCharToMultiByte(CP_ACP, 0, value, -1, szValue, len, NULL, NULL);
    else if (szValue)
        ret = ERROR_MORE_DATA;

    *pcchValue = len - 1;

done:
    msi_free(product);
    msi_free(usersid);
    msi_free(property);
    msi_free(value);
    return ret;
}

/******************************************************************
 *  MsiSourceListGetInfoW   (MSI.@)
 */
UINT WINAPI MsiSourceListGetInfoW( LPCWSTR szProduct, LPCWSTR szUserSid,
                                   MSIINSTALLCONTEXT dwContext, DWORD dwOptions,
                                   LPCWSTR szProperty, LPWSTR szValue, 
                                   LPDWORD pcchValue) 
{
    WCHAR squished_pc[GUID_SIZE];
    HKEY sourcekey, media;
    LPWSTR source, ptr;
    DWORD size;
    UINT rc;

    static const WCHAR mediapack[] = {
        'M','e','d','i','a','P','a','c','k','a','g','e',0};

    TRACE("%s %s\n", debugstr_w(szProduct), debugstr_w(szProperty));

    if (!szProduct || !squash_guid(szProduct, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (szValue && !pcchValue)
        return ERROR_INVALID_PARAMETER;

    if (dwContext != MSIINSTALLCONTEXT_USERMANAGED &&
        dwContext != MSIINSTALLCONTEXT_USERUNMANAGED &&
        dwContext != MSIINSTALLCONTEXT_MACHINE)
        return ERROR_INVALID_PARAMETER;

    if (!szProperty)
        return ERROR_INVALID_PARAMETER;

    if (szUserSid)
        FIXME("Unhandled UserSid %s\n",debugstr_w(szUserSid));

    if (dwContext != MSIINSTALLCONTEXT_USERUNMANAGED)
        FIXME("Unhandled context %d\n", dwContext);

    rc = OpenSourceKey(szProduct, &sourcekey, dwOptions, dwContext, FALSE);
    if (rc != ERROR_SUCCESS)
        return rc;

    if (!lstrcmpW(szProperty, INSTALLPROPERTY_MEDIAPACKAGEPATHW) ||
        !lstrcmpW(szProperty, INSTALLPROPERTY_DISKPROMPTW))
    {
        rc = OpenMediaSubkey(sourcekey, &media, FALSE);
        if (rc != ERROR_SUCCESS)
        {
            RegCloseKey(sourcekey);
            return ERROR_SUCCESS;
        }

        if (!lstrcmpW(szProperty, INSTALLPROPERTY_MEDIAPACKAGEPATHW))
            szProperty = mediapack;

        RegQueryValueExW(media, szProperty, 0, 0, (LPBYTE)szValue, pcchValue);
        RegCloseKey(media);
    }
    else if (!lstrcmpW(szProperty, INSTALLPROPERTY_LASTUSEDSOURCEW) ||
             !lstrcmpW(szProperty, INSTALLPROPERTY_LASTUSEDTYPEW))
    {
        rc = RegQueryValueExW(sourcekey, INSTALLPROPERTY_LASTUSEDSOURCEW,
                              0, 0, NULL, &size);
        if (rc != ERROR_SUCCESS)
        {
            RegCloseKey(sourcekey);
            return ERROR_SUCCESS;
        }

        source = msi_alloc(size);
        RegQueryValueExW(sourcekey, INSTALLPROPERTY_LASTUSEDSOURCEW,
                         0, 0, (LPBYTE)source, &size);

        if (!*source)
        {
            msi_free(source);
            RegCloseKey(sourcekey);
            return ERROR_SUCCESS;
        }

        if (!lstrcmpW(szProperty, INSTALLPROPERTY_LASTUSEDTYPEW))
        {
            if (*source != 'n' && *source != 'u' && *source != 'm')
            {
                msi_free(source);
                RegCloseKey(sourcekey);
                return ERROR_SUCCESS;
            }

            ptr = source;
            source[1] = '\0';
        }
        else
        {
            ptr = strrchrW(source, ';');
            if (!ptr)
                ptr = source;
            else
                ptr++;
        }

        if (szValue)
        {
            if (lstrlenW(ptr) < *pcchValue)
                lstrcpyW(szValue, ptr);
            else
                rc = ERROR_MORE_DATA;
        }

        *pcchValue = lstrlenW(ptr);
        msi_free(source);
    }
    else if (strcmpW(INSTALLPROPERTY_PACKAGENAMEW, szProperty)==0)
    {
        *pcchValue = *pcchValue * sizeof(WCHAR);
        rc = RegQueryValueExW(sourcekey, INSTALLPROPERTY_PACKAGENAMEW, 0, 0,
                              (LPBYTE)szValue, pcchValue);
        if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA)
        {
            *pcchValue = 0;
            rc = ERROR_SUCCESS;
        }
        else
        {
            if (*pcchValue)
                *pcchValue = (*pcchValue - 1) / sizeof(WCHAR);
            if (szValue)
                szValue[*pcchValue] = '\0';
        }
    }
    else
    {
        FIXME("Unknown property %s\n",debugstr_w(szProperty));
        rc = ERROR_UNKNOWN_PROPERTY;
    }

    RegCloseKey(sourcekey);
    return rc;
}

/******************************************************************
 *  MsiSourceListSetInfoA   (MSI.@)
 */
UINT WINAPI MsiSourceListSetInfoA(LPCSTR szProduct, LPCSTR szUserSid,
                                  MSIINSTALLCONTEXT dwContext, DWORD dwOptions,
                                  LPCSTR szProperty, LPCSTR szValue)
{
    UINT ret;
    LPWSTR product = NULL;
    LPWSTR usersid = NULL;
    LPWSTR property = NULL;
    LPWSTR value = NULL;

    if (szProduct) product = strdupAtoW(szProduct);
    if (szUserSid) usersid = strdupAtoW(szUserSid);
    if (szProperty) property = strdupAtoW(szProperty);
    if (szValue) value = strdupAtoW(szValue);

    ret = MsiSourceListSetInfoW(product, usersid, dwContext, dwOptions,
                                property, value);

    msi_free(product);
    msi_free(usersid);
    msi_free(property);
    msi_free(value);

    return ret;
}

UINT msi_set_last_used_source(LPCWSTR product, LPCWSTR usersid,
                              MSIINSTALLCONTEXT context, DWORD options,
                              LPCWSTR value)
{
    HKEY source;
    LPWSTR buffer;
    WCHAR typechar;
    DWORD size;
    UINT r;
    int index = 1;

    static const WCHAR format[] = {'%','c',';','%','i',';','%','s',0};

    if (options & MSISOURCETYPE_NETWORK)
        typechar = 'n';
    else if (options & MSISOURCETYPE_URL)
        typechar = 'u';
    else if (options & MSISOURCETYPE_MEDIA)
        typechar = 'm';
    else
        return ERROR_INVALID_PARAMETER;

    if (!(options & MSISOURCETYPE_MEDIA))
    {
        r = MsiSourceListAddSourceExW(product, usersid, context,
                                      options, value, 0);
        if (r != ERROR_SUCCESS)
            return r;

        index = 0;
        while ((r = MsiSourceListEnumSourcesW(product, usersid, context, options,
                                              index, NULL, NULL)) == ERROR_SUCCESS)
            index++;

        if (r != ERROR_NO_MORE_ITEMS)
            return r;
    }

    size = (lstrlenW(format) + lstrlenW(value) + 7) * sizeof(WCHAR);
    buffer = msi_alloc(size);
    if (!buffer)
        return ERROR_OUTOFMEMORY;

    r = OpenSourceKey(product, &source, MSICODE_PRODUCT, context, FALSE);
    if (r != ERROR_SUCCESS)
        return r;

    sprintfW(buffer, format, typechar, index, value);

    size = (lstrlenW(buffer) + 1) * sizeof(WCHAR);
    r = RegSetValueExW(source, INSTALLPROPERTY_LASTUSEDSOURCEW, 0,
                       REG_SZ, (LPBYTE)buffer, size);
    msi_free(buffer);

    RegCloseKey(source);
    return r;
}

/******************************************************************
 *  MsiSourceListSetInfoW   (MSI.@)
 */
UINT WINAPI MsiSourceListSetInfoW( LPCWSTR szProduct, LPCWSTR szUserSid,
                                   MSIINSTALLCONTEXT dwContext, DWORD dwOptions,
                                   LPCWSTR szProperty, LPCWSTR szValue)
{
    WCHAR squished_pc[GUID_SIZE];
    HKEY sourcekey, media;
    LPCWSTR property;
    UINT rc;

    static const WCHAR media_package[] = {
        'M','e','d','i','a','P','a','c','k','a','g','e',0
    };

    TRACE("%s %s %x %x %s %s\n", debugstr_w(szProduct), debugstr_w(szUserSid),
            dwContext, dwOptions, debugstr_w(szProperty), debugstr_w(szValue));

    if (!szProduct || !squash_guid(szProduct, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (!szProperty)
        return ERROR_INVALID_PARAMETER;

    if (!szValue)
        return ERROR_UNKNOWN_PROPERTY;

    if (dwContext == MSIINSTALLCONTEXT_MACHINE && szUserSid)
        return ERROR_INVALID_PARAMETER;

    if (dwOptions & MSICODE_PATCH)
    {
        FIXME("Unhandled options MSICODE_PATCH\n");
        return ERROR_UNKNOWN_PATCH;
    }

    property = szProperty;
    if (!lstrcmpW(szProperty, INSTALLPROPERTY_MEDIAPACKAGEPATHW))
        property = media_package;

    rc = OpenSourceKey(szProduct, &sourcekey, MSICODE_PRODUCT, dwContext, FALSE);
    if (rc != ERROR_SUCCESS)
        return rc;

    if (lstrcmpW(szProperty, INSTALLPROPERTY_LASTUSEDSOURCEW) &&
        dwOptions & (MSISOURCETYPE_NETWORK | MSISOURCETYPE_URL))
    {
        RegCloseKey(sourcekey);
        return ERROR_INVALID_PARAMETER;
    }

    if (!lstrcmpW(szProperty, INSTALLPROPERTY_MEDIAPACKAGEPATHW) ||
        !lstrcmpW(szProperty, INSTALLPROPERTY_DISKPROMPTW))
    {
        rc = OpenMediaSubkey(sourcekey, &media, TRUE);
        if (rc == ERROR_SUCCESS)
        {
            rc = msi_reg_set_val_str(media, property, szValue);
            RegCloseKey(media);
        }
    }
    else if (strcmpW(INSTALLPROPERTY_PACKAGENAMEW, szProperty)==0)
    {
        DWORD size = (lstrlenW(szValue) + 1) * sizeof(WCHAR);
        rc = RegSetValueExW(sourcekey, INSTALLPROPERTY_PACKAGENAMEW, 0,
                REG_SZ, (const BYTE *)szValue, size);
        if (rc != ERROR_SUCCESS)
            rc = ERROR_UNKNOWN_PROPERTY;
    }
    else if (!lstrcmpW(szProperty, INSTALLPROPERTY_LASTUSEDSOURCEW))
    {
        if (!(dwOptions & (MSISOURCETYPE_NETWORK | MSISOURCETYPE_URL)))
            rc = ERROR_INVALID_PARAMETER;
        else
            rc = msi_set_last_used_source(szProduct, szUserSid, dwContext,
                                          dwOptions, szValue);
    }
    else
        rc = ERROR_UNKNOWN_PROPERTY;

    RegCloseKey(sourcekey);
    return rc;
}

/******************************************************************
 *  MsiSourceListAddSourceW (MSI.@)
 */
UINT WINAPI MsiSourceListAddSourceW( LPCWSTR szProduct, LPCWSTR szUserName,
        DWORD dwReserved, LPCWSTR szSource)
{
    WCHAR squished_pc[GUID_SIZE];
    INT ret;
    LPWSTR sidstr = NULL;
    DWORD sidsize = 0;
    DWORD domsize = 0;
    DWORD context;
    HKEY hkey = 0;
    UINT r;

    TRACE("%s %s %s\n", debugstr_w(szProduct), debugstr_w(szUserName), debugstr_w(szSource));

    if (!szSource || !*szSource)
        return ERROR_INVALID_PARAMETER;

    if (dwReserved != 0)
        return ERROR_INVALID_PARAMETER;

    if (!szProduct || !squash_guid(szProduct, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (!szUserName || !*szUserName)
        context = MSIINSTALLCONTEXT_MACHINE;
    else
    {
        if (LookupAccountNameW(NULL, szUserName, NULL, &sidsize, NULL, &domsize, NULL))
        {
            PSID psid = msi_alloc(sidsize);

            if (LookupAccountNameW(NULL, szUserName, psid, &sidsize, NULL, &domsize, NULL))
                ConvertSidToStringSidW(psid, &sidstr);

            msi_free(psid);
        }

        r = MSIREG_OpenLocalManagedProductKey(szProduct, &hkey, FALSE);
        if (r == ERROR_SUCCESS)
            context = MSIINSTALLCONTEXT_USERMANAGED;
        else
        {
            r = MSIREG_OpenUserProductsKey(szProduct, &hkey, FALSE);
            if (r != ERROR_SUCCESS)
                return ERROR_UNKNOWN_PRODUCT;

            context = MSIINSTALLCONTEXT_USERUNMANAGED;
        }

        RegCloseKey(hkey);
    }

    ret = MsiSourceListAddSourceExW(szProduct, sidstr, 
        context, MSISOURCETYPE_NETWORK, szSource, 0);

    if (sidstr)
        LocalFree(sidstr);

    return ret;
}

/******************************************************************
 *  MsiSourceListAddSourceA (MSI.@)
 */
UINT WINAPI MsiSourceListAddSourceA( LPCSTR szProduct, LPCSTR szUserName,
        DWORD dwReserved, LPCSTR szSource)
{
    INT ret;
    LPWSTR szwproduct;
    LPWSTR szwusername;
    LPWSTR szwsource;

    szwproduct = strdupAtoW( szProduct );
    szwusername = strdupAtoW( szUserName );
    szwsource = strdupAtoW( szSource );

    ret = MsiSourceListAddSourceW(szwproduct, szwusername, dwReserved, szwsource);

    msi_free(szwproduct);
    msi_free(szwusername);
    msi_free(szwsource);

    return ret;
}

/******************************************************************
 *  MsiSourceListAddSourceExA (MSI.@)
 */
UINT WINAPI MsiSourceListAddSourceExA(LPCSTR szProduct, LPCSTR szUserSid,
        MSIINSTALLCONTEXT dwContext, DWORD dwOptions, LPCSTR szSource, DWORD dwIndex)
{
    UINT ret;
    LPWSTR product, usersid, source;

    product = strdupAtoW(szProduct);
    usersid = strdupAtoW(szUserSid);
    source = strdupAtoW(szSource);

    ret = MsiSourceListAddSourceExW(product, usersid, dwContext,
                                    dwOptions, source, dwIndex);

    msi_free(product);
    msi_free(usersid);
    msi_free(source);

    return ret;
}

static void free_source_list(struct list *sourcelist)
{
    while (!list_empty(sourcelist))
    {
        media_info *info = LIST_ENTRY(list_head(sourcelist), media_info, entry);
        list_remove(&info->entry);
        msi_free(info->path);
        msi_free(info);
    }
}

static void add_source_to_list(struct list *sourcelist, media_info *info,
                               DWORD *index)
{
    media_info *iter;
    BOOL found = FALSE;
    static const WCHAR fmt[] = {'%','i',0};

    if (index) *index = 0;

    if (list_empty(sourcelist))
    {
        list_add_head(sourcelist, &info->entry);
        return;
    }

    LIST_FOR_EACH_ENTRY(iter, sourcelist, media_info, entry)
    {
        if (!found && info->index < iter->index)
        {
            found = TRUE;
            list_add_before(&iter->entry, &info->entry);
        }

        /* update the rest of the list */
        if (found)
            sprintfW(iter->szIndex, fmt, ++iter->index);
        else if (index)
            (*index)++;
    }

    if (!found)
        list_add_after(&iter->entry, &info->entry);
}

static UINT fill_source_list(struct list *sourcelist, HKEY sourcekey, DWORD *count)
{
    UINT r = ERROR_SUCCESS;
    DWORD index = 0;
    WCHAR name[10];
    DWORD size, val_size;
    media_info *entry;

    *count = 0;

    while (r == ERROR_SUCCESS)
    {
        size = sizeof(name) / sizeof(name[0]);
        r = RegEnumValueW(sourcekey, index, name, &size, NULL, NULL, NULL, &val_size);
        if (r != ERROR_SUCCESS)
            return r;

        entry = msi_alloc(sizeof(media_info));
        if (!entry)
            goto error;

        entry->path = msi_alloc(val_size);
        if (!entry->path)
        {
            msi_free(entry);
            goto error;
        }

        lstrcpyW(entry->szIndex, name);
        entry->index = atoiW(name);

        size++;
        r = RegEnumValueW(sourcekey, index, name, &size, NULL,
                          NULL, (LPBYTE)entry->path, &val_size);
        if (r != ERROR_SUCCESS)
        {
            msi_free(entry->path);
            msi_free(entry);
            goto error;
        }

        index = ++(*count);
        add_source_to_list(sourcelist, entry, NULL);
    }

error:
    *count = -1;
    free_source_list(sourcelist);
    return ERROR_OUTOFMEMORY;
}

/******************************************************************
 *  MsiSourceListAddSourceExW (MSI.@)
 */
UINT WINAPI MsiSourceListAddSourceExW( LPCWSTR szProduct, LPCWSTR szUserSid,
        MSIINSTALLCONTEXT dwContext, DWORD dwOptions, LPCWSTR szSource, 
        DWORD dwIndex)
{
    HKEY sourcekey;
    HKEY typekey;
    UINT rc;
    struct list sourcelist;
    media_info *info;
    WCHAR squished_pc[GUID_SIZE];
    WCHAR name[10];
    LPWSTR source;
    LPCWSTR postfix;
    DWORD size, count;
    DWORD index;

    static const WCHAR fmt[] = {'%','i',0};
    static const WCHAR one[] = {'1',0};
    static const WCHAR backslash[] = {'\\',0};
    static const WCHAR forwardslash[] = {'/',0};

    TRACE("%s %s %x %x %s %i\n", debugstr_w(szProduct), debugstr_w(szUserSid),
          dwContext, dwOptions, debugstr_w(szSource), dwIndex);

    if (!szProduct || !squash_guid(szProduct, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (!szSource || !*szSource)
        return ERROR_INVALID_PARAMETER;

    if (!(dwOptions & (MSISOURCETYPE_NETWORK | MSISOURCETYPE_URL)))
        return ERROR_INVALID_PARAMETER;

    if (dwOptions & MSICODE_PATCH)
    {
        FIXME("Unhandled options MSICODE_PATCH\n");
        return ERROR_FUNCTION_FAILED;
    }

    if (szUserSid && (dwContext & MSIINSTALLCONTEXT_MACHINE))
        return ERROR_INVALID_PARAMETER;

    rc = OpenSourceKey(szProduct, &sourcekey, MSICODE_PRODUCT, dwContext, FALSE);
    if (rc != ERROR_SUCCESS)
        return rc;

    if (dwOptions & MSISOURCETYPE_NETWORK)
        rc = OpenNetworkSubkey(sourcekey, &typekey, TRUE);
    else if (dwOptions & MSISOURCETYPE_URL)
        rc = OpenURLSubkey(sourcekey, &typekey, TRUE);
    else if (dwOptions & MSISOURCETYPE_MEDIA)
        rc = OpenMediaSubkey(sourcekey, &typekey, TRUE);
    else
    {
        ERR("unknown media type: %08x\n", dwOptions);
        RegCloseKey(sourcekey);
        return ERROR_FUNCTION_FAILED;
    }

    postfix = (dwOptions & MSISOURCETYPE_NETWORK) ? backslash : forwardslash;
    if (szSource[lstrlenW(szSource) - 1] == *postfix)
        source = strdupW(szSource);
    else
    {
        size = lstrlenW(szSource) + 2;
        source = msi_alloc(size * sizeof(WCHAR));
        lstrcpyW(source, szSource);
        lstrcatW(source, postfix);
    }

    list_init(&sourcelist);
    rc = fill_source_list(&sourcelist, typekey, &count);
    if (rc != ERROR_NO_MORE_ITEMS)
        return rc;

    size = (lstrlenW(source) + 1) * sizeof(WCHAR);

    if (count == 0)
    {
        rc = RegSetValueExW(typekey, one, 0, REG_EXPAND_SZ, (LPBYTE)source, size);
        goto done;
    }
    else if (dwIndex > count || dwIndex == 0)
    {
        sprintfW(name, fmt, count + 1);
        rc = RegSetValueExW(typekey, name, 0, REG_EXPAND_SZ, (LPBYTE)source, size);
        goto done;
    }
    else
    {
        sprintfW(name, fmt, dwIndex);
        info = msi_alloc(sizeof(media_info));
        if (!info)
        {
            rc = ERROR_OUTOFMEMORY;
            goto done;
        }

        info->path = strdupW(source);
        lstrcpyW(info->szIndex, name);
        info->index = dwIndex;
        add_source_to_list(&sourcelist, info, &index);

        LIST_FOR_EACH_ENTRY(info, &sourcelist, media_info, entry)
        {
            if (info->index < index)
                continue;

            size = (lstrlenW(info->path) + 1) * sizeof(WCHAR);
            rc = RegSetValueExW(typekey, info->szIndex, 0,
                                REG_EXPAND_SZ, (LPBYTE)info->path, size);
            if (rc != ERROR_SUCCESS)
                goto done;
        }
    }

done:
    free_source_list(&sourcelist);
    msi_free(source);
    RegCloseKey(typekey);
    RegCloseKey(sourcekey);
    return rc;
}

/******************************************************************
 *  MsiSourceListAddMediaDiskA (MSI.@)
 */
UINT WINAPI MsiSourceListAddMediaDiskA(LPCSTR szProduct, LPCSTR szUserSid,
        MSIINSTALLCONTEXT dwContext, DWORD dwOptions, DWORD dwDiskId,
        LPCSTR szVolumeLabel, LPCSTR szDiskPrompt)
{
    UINT r;
    LPWSTR product = NULL;
    LPWSTR usersid = NULL;
    LPWSTR volume = NULL;
    LPWSTR prompt = NULL;

    if (szProduct) product = strdupAtoW(szProduct);
    if (szUserSid) usersid = strdupAtoW(szUserSid);
    if (szVolumeLabel) volume = strdupAtoW(szVolumeLabel);
    if (szDiskPrompt) prompt = strdupAtoW(szDiskPrompt);

    r = MsiSourceListAddMediaDiskW(product, usersid, dwContext, dwOptions,
                                     dwDiskId, volume, prompt);

    msi_free(product);
    msi_free(usersid);
    msi_free(volume);
    msi_free(prompt);

    return r;
}

/******************************************************************
 *  MsiSourceListAddMediaDiskW (MSI.@)
 */
UINT WINAPI MsiSourceListAddMediaDiskW(LPCWSTR szProduct, LPCWSTR szUserSid, 
        MSIINSTALLCONTEXT dwContext, DWORD dwOptions, DWORD dwDiskId, 
        LPCWSTR szVolumeLabel, LPCWSTR szDiskPrompt)
{
    HKEY sourcekey;
    HKEY mediakey;
    UINT rc;
    WCHAR szIndex[10];
    WCHAR squished_pc[GUID_SIZE];
    LPWSTR buffer;
    DWORD size;

    static const WCHAR fmt[] = {'%','i',0};
    static const WCHAR semicolon[] = {';',0};

    TRACE("%s %s %x %x %i %s %s\n", debugstr_w(szProduct),
            debugstr_w(szUserSid), dwContext, dwOptions, dwDiskId,
            debugstr_w(szVolumeLabel), debugstr_w(szDiskPrompt));

    if (!szProduct || !squash_guid(szProduct, squished_pc))
        return ERROR_INVALID_PARAMETER;

    if (dwOptions != MSICODE_PRODUCT && dwOptions != MSICODE_PATCH)
        return ERROR_INVALID_PARAMETER;

    if ((szVolumeLabel && !*szVolumeLabel) || (szDiskPrompt && !*szDiskPrompt))
        return ERROR_INVALID_PARAMETER;

    if ((dwContext & MSIINSTALLCONTEXT_MACHINE) && szUserSid)
        return ERROR_INVALID_PARAMETER;

    if (dwOptions & MSICODE_PATCH)
    {
        FIXME("Unhandled options MSICODE_PATCH\n");
        return ERROR_FUNCTION_FAILED;
    }

    rc = OpenSourceKey(szProduct, &sourcekey, MSICODE_PRODUCT, dwContext, FALSE);
    if (rc != ERROR_SUCCESS)
        return rc;

    OpenMediaSubkey(sourcekey, &mediakey, TRUE);

    sprintfW(szIndex, fmt, dwDiskId);

    size = 2;
    if (szVolumeLabel) size += lstrlenW(szVolumeLabel);
    if (szDiskPrompt) size += lstrlenW(szDiskPrompt);

    size *= sizeof(WCHAR);
    buffer = msi_alloc(size);
    *buffer = '\0';

    if (szVolumeLabel) lstrcpyW(buffer, szVolumeLabel);
    lstrcatW(buffer, semicolon);
    if (szDiskPrompt) lstrcatW(buffer, szDiskPrompt);

    RegSetValueExW(mediakey, szIndex, 0, REG_SZ, (LPBYTE)buffer, size);
    msi_free(buffer);

    RegCloseKey(sourcekey);
    RegCloseKey(mediakey);

    return ERROR_SUCCESS;
}

/******************************************************************
 *  MsiSourceListClearAllA (MSI.@)
 */
UINT WINAPI MsiSourceListClearAllA( LPCSTR szProduct, LPCSTR szUserName, DWORD dwReserved )
{
    FIXME("(%s %s %d)\n", debugstr_a(szProduct), debugstr_a(szUserName), dwReserved);
    return ERROR_SUCCESS;
}

/******************************************************************
 *  MsiSourceListClearAllW (MSI.@)
 */
UINT WINAPI MsiSourceListClearAllW( LPCWSTR szProduct, LPCWSTR szUserName, DWORD dwReserved )
{
    FIXME("(%s %s %d)\n", debugstr_w(szProduct), debugstr_w(szUserName), dwReserved);
    return ERROR_SUCCESS;
}
