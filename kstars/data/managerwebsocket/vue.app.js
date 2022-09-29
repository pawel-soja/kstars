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


const supportsTouch = ('ontouchstart' in window);

var app = Vue.createApp({
    data: function() {
        return {
            status: 'Offline',
            connection: null,
            logOutput: [],

            // ekos reponse
            ekos: {
                mount: {
                    slewRate: 1,
                    equatorialCoords: [0, 0],
                    horizontalCoords: [0, 0]
                }
            },

            // internal state
            scope: {
                focus: {
                    stepLevel: 2,
                    step: 100,
                }
            }
        }
    },
    watch: {
        'ekos.mount.slewRate'(value) {
            this.invoke('ekos.mount.setSlewRate', value);
        },
        'scope.focus.stepLevel'(value) {
            this.scope.focus.step = Math.pow(10, value);
        },
    },
    methods: {
        doHello() {
            this.send({
                ekos: {
                    mount: {
                        slewRate: []
                    }
                }
            });
        },
        onRefresh()
        {
            this.send({
                ekos: {
                    mount: {
                        equatorialCoords: [],
                        horizontalCoords: []
                    }
                }
            })
        },
        connect() {
            this.connection = new WebSocket(serverAddress);

            this.connection.onmessage = (e) => {
                let data = JSON.parse(e.data);
                this.debug('receive', JSON.stringify(data));
                if (!Object.isEmpty(data.ekos))
                    this.ekos.merge(data.ekos);
            }

            this.connection.onopen = () => {
                this.status = 'Online';
                this.doHello();
                this.onRefresh();
                this.onRefreshTimer = setInterval(() => this.onRefresh(), 1000);
            }

            this.connection.onerror = () => {
                this.status = 'Error';
                this.connection.close();
            }

            this.connection.onclose = () => {
                clearInterval(this.onRefreshTimer);
                this.status = 'Offline';
                setTimeout(() => {
                    this.connect();
                }, 1000);
            }
        },

        debug(...args)
        {
            //this.logOutput = [...this.logOutput, args.join(' ')];
            console.log(args.join(' '));
        },

        send(value) {
            this.debug('   send', JSON.stringify(value));
            this.connection.send(JSON.stringify(value));
        },

        invoke(path, ...args) {
            this.send(path.split('.').reverse().reduce((p, n) => ({[n]: p}), [...args]));
        },

    },

    mounted() {
        this.connect();
    },

    directives: {
        pressed: {
            created(el, binding) {
                el.addEventListener(supportsTouch ? 'touchstart' : 'mousedown', () => {
                    el.pressed = true;
                    binding.value();
                });
            }
        },
        released: {
            created(el, binding) {
                document.addEventListener(supportsTouch ? 'touchend' : 'mouseup', () => {
                    if (el.pressed)
                        binding.value();
                    el.pressed = false;
                });
            }
        }
    }
});

app.config.globalProperties.$filters = {
    toHHMMSS: (v, f =['h', 'm', 's'])     => f.reduce((p, n) => (i = parseInt(v), v = (v - i) * 60, [...p, (i).toString().padStart(3, ' ') + n]), []).join(''),
    toDDMMSS: (v, f =['\xB0', '\'', '"']) => f.reduce((p, n) => (i = parseInt(v), v = (v - i) * 60, [...p, (i).toString().padStart(3, ' ') + n]), []).join(''),
};

app.mount('#app');
