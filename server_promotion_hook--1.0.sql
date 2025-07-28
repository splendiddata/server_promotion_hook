/*
 * Copyright (c) Splendid Data Product Development B.V. 2025
 *
 * This program is free software: You may redistribute and/or modify under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at Client's option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, Client should obtain one via www.gnu.org/licenses/.
 */

comment on schema  @extschema@  is 'Belongs to the server_promotion_hook extension';
grant usage on schema  @extschema@  to public;

/*
 * server_promotion_hook.is_executing_server_promotion_hook() true if the server_promotion_hook.on_server_promotion() function
 * is currently executing under control of the server_promotion_hook logic. Thus the 
 * on_server_promotion() function can check if it is invoked as part of the login process or
 * if the a is trying to execute it, maybe causing a problem. 
 */
create function server_promotion_hook.is_executing_server_promotion_hook()
    returns boolean 
    language C
    security definer leakproof
    as 'server_promotion_hook.so', 'is_executing_server_promotion_hook';
comment on function server_promotion_hook.is_executing_server_promotion_hook() is
    'Returns true if the server_promotion_hook.on_server_promotion() function is executed under control of the server_promotion_hook logic';
grant execute on function server_promotion_hook.is_executing_server_promotion_hook() to public;

/*
 * server_promotion_hook.get_server_promotion_hook_version() just returns the current code version
 * of the server_promotion_hook database extension.
 */
create function server_promotion_hook.get_server_promotion_hook_version()
    returns text 
    immutable leakproof
    as 'server_promotion_hook.so', 'get_server_promotion_hook_version'
    language C
    security definer;
comment on function server_promotion_hook.get_server_promotion_hook_version() is
    'Returns the version of this database'' server_promotion_hook database extension';
grant execute on function server_promotion_hook.get_server_promotion_hook_version() to public;
