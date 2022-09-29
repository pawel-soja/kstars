/*
    Copyright (C) 2022 by Pawel Soja <kernel32.pl@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Deep copy object
Object.prototype.clone = function(o) {
    return JSON.parse(JSON.stringify(o || this));
}

// return difference between objects
Object.prototype.diffStruct = function(a, b) {
    let path = arguments[2] || [];
    if (Object.isStruct(a))
        return Object.keys(a).reduce((r, k) => ({...r, ...Object.diffStruct(a[k], b[k], [k, ...path])}), {});

    return Object.isEquals(a, b) ? {} : [...path].reduce((p, n) => ({[n]: p}), a);
}

// merge this object with other
Object.prototype.merge = function(other) {
    for (const key in other) {
        //if (this.hasOwnProperty(key) && (Object.isStruct(this[key]) || Object.isStruct(other[key])))
        if (this.hasOwnProperty(key) && (Object.isStruct(other[key]))) // Warning: can reduce deep
            this[key].merge(other[key]);
        else
            this[key] = other[key];
    }
}

// check is object, Object.isObject({}) == true, Object.isObject([]) == true
Object.prototype.isObject = function(o) {
    return typeof o === 'object' && o !== null;
}

// check is object, Object.isStruct({}) == true, Object.isStruct([]) == false
Object.prototype.isStruct = function(o) {
    return Object.isObject(o) && !Array.isArray(o);
}

// check is object/array is empty
Object.prototype.isEmpty = function(o) {
    return Object.keys(o || this).length === 0;
}

// compare two object/array/values
Object.prototype.isEquals = function(a, b) {
    // check Array
    if (Array.isArray(a) && Array.isArray(b)) {
        if (a.length != b.length)
            return false;

        for (let i=0, l=a.length; i<l; ++i)
            if (!Object.isEquals(a[i], b[i]))
                return false;

        return true;
    }

    // object mismatch
    if (Array.isArray(a) || Array.isArray(b))
        return false;

    // check Object
    if (Object.isObject(a) && Object.isObject(b)) {
        if (!Object.isEquals(Object.keys(a).sort(), Object.keys(b).sort()))
            return false;

        for (const key in a)
            if (!Object.isEquals(a[key], b[key]))
                return false;

        return true;
    }

    return a == b;
}
