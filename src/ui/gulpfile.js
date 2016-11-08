/*
// Copyright (c) 2015 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
'use strict';

var gulp = require('gulp'),
    gulpUtil = require('gulp-util'),
    uglify = require('gulp-uglify'),
    htmlmin = require('gulp-htmlmin'),
    gInject = require('gulp-inject'),
    mainBowerFiles = require('main-bower-files'),
    connect = require('gulp-connect'),
    jshint = require('gulp-jshint'),
    minifyCSS = require('gulp-minify-css'),
    templateCache = require('gulp-angular-templatecache'),
    sass = require('gulp-sass'),
    // image min installation fails behind proxy removed for now
    // imagemin = require('gulp-imagemin'),
    program = require('commander'),
    stylish = require('jshint-stylish'),
    gulpFilter = require('gulp-filter'),
    concat = require('gulp-concat'),
    notify = require('gulp-notify'),
    rev = require('gulp-rev'),
    rm = require('gulp-rm'),

    debug = false,
    config = {
      buildDir: '../../satt/visualize/webui',
      publicDir: 'public',
      fonts: '/fonts',
      js: '/js',
      css: '/css',
      img: '/css/img',
      lib: '/lib'
    },
    WATCH_MODE = 'watch',
    RUN_MODE = 'run';

var mode = RUN_MODE;

program
  .version('1.0.0')
  .parse(process.argv);

gulp.task('js', function () {
  var jsTask = gulp.src('app/scripts/**/*.js');
  if (!debug) {
    jsTask.pipe(uglify().on('error', gulpUtil.log));
  }
  return jsTask.pipe(gulp.dest('public/js'))
    .pipe(connect.reload());
});

gulp.task('template', function () {
  var templateTask = gulp.src('app/views/**/*.html');
  if (!debug) {
    templateTask.pipe(htmlmin({ collapseWhitespace: true }));
  }
  return templateTask.pipe(templateCache({ standalone:true, root: 'views/'}))
  .pipe(connect.reload())
  .pipe(gulp.dest(config.publicDir + config.js));
});

var jsFilter = gulpFilter('**/*.js');

gulp.task('bower', function () {
  var bower = gulp.src(mainBowerFiles())
    .pipe(jsFilter);
  if (!debug) {
    bower.pipe(uglify().on('error', gulpUtil.log));
  }
  return bower.pipe(connect.reload())
  .pipe(gulp.dest(config.publicDir + config.lib));
});

gulp.task('index', ['assets'], function () {
  var target = gulp.src('app/index.html');
  var sources = gulp.src(['public/js/**/*.js','public/css/**/*.css'], {read: false});
  return target.pipe(gInject(sources,
      { relative: false, ignorePath: 'public', addRootSlash: false }))
    .pipe(gulp.dest(config.publicDir));
});

gulp.task('icons', function () {
  return gulp.src('app/components/font-awesome/fonts/*')
    .pipe(gulp.dest(config.publicDir + config.fonts));
});

gulp.task('css', function () {
  var options = {
    errLogToConsole: true,
    includePaths: ['.',
      'app/components/bootstrap-sass/vendor/assets/stylesheets',
    ],
  };
  if (!debug) {
    options.outputStyle = 'expanded';
    options.sourceComments = 'map';
  }
  var cssTask = gulp.src(['app/styles/main.scss'])
    .pipe(sass(options));
  if (!debug) {
    cssTask.pipe(minifyCSS());
  }
  return cssTask.pipe(gulp.dest(config.publicDir + config.css))
    .pipe(connect.reload());
});

gulp.task('image', function () {
  return gulp.src(['app/styles/img/**.png',
    'app/styles/img/**.gif',
    'app/components/bootstrap-sass/vendor/assets/images/**.png'])
    //.pipe(imagemin())
    .pipe(gulp.dest('public/css/img'))
    .pipe(connect.reload());
});

gulp.task('lint', function () {
  return gulp.src('app/scripts/**/*.js')
    .pipe(jshint())
    .pipe(jshint.reporter(stylish));
});

gulp.task('connect', function () {
  if (mode === WATCH_MODE) {
    gulp.watch(['index.html'], function () {
      gulp.src(['index.html'])
        .pipe(connect.reload());
    });
  }

  connect.server({
    root: 'public/',
    port: 3501,
    livereload: mode === WATCH_MODE
  });
});

gulp.task('debug', function () {
  debug = true;
});

gulp.task('watch-mode', function () {
  mode = WATCH_MODE;

  var jsWatcher = gulp.watch('app/scripts/**/*.js', ['js']),
    cssWatcher = gulp.watch('app/styles/**/*.scss', ['css']),
    imageWatcher = gulp.watch('app/style/img/**/*', ['image']),
    htmlWatcher = gulp.watch('app/views/**/*.html', ['template']);

  function changeNotification(event) {
    console.log('File', event.path, 'was', event.type, ', running tasks...');
  }

  jsWatcher.on('change', changeNotification);
  cssWatcher.on('change', changeNotification);
  imageWatcher.on('change', changeNotification);
  htmlWatcher.on('change', changeNotification);
});

gulp.task('clean', function () {
  return gulp.src([config.buildDir + '/**/*',config.buildDir + '/**/.*'], { read: false })
    .pipe(rm());
});

gulp.task('release-js', ['all','clean'], function () {
  return gulp.src(config.publicDir + config.js + '/**/*.js')
    .pipe(concat('all.js'))
    .pipe(rev())
    .pipe(uglify().on('error', gulpUtil.log))
    .pipe(gulp.dest(config.buildDir + config.js))
    .pipe(notify({ message: 'JS task complete' }));
});

gulp.task('release-fonts', ['all', 'clean'], function () {
  return gulp.src(config.publicDir + config.fonts + '/**.*')
    .pipe(gulp.dest(config.buildDir + config.fonts))
    .pipe(notify({ message: 'Fonts task complete' }));
});

gulp.task('release-lib', ['all','clean'], function () {
  return gulp.src(config.publicDir + config.lib + '/**.*')
    .pipe(gulp.dest(config.buildDir + config.lib))
    .pipe(notify({ message: 'Lib task complete'}));
});

gulp.task('release-css', ['all', 'clean'], function () {
  return  gulp.src(config.publicDir + config.css + '/*.css')
    .pipe(rev())
    .pipe(gulp.dest(config.buildDir + config.css))
    .pipe(notify({ message: 'CSS task complete'}));
});

gulp.task('release-img', ['all', 'clean'], function () {
  return gulp.src([config.publicDir + config.img + '/*.png',
      config.publicDir + config.img + '/*.gif'])
    .pipe(gulp.dest(config.buildDir + config.img))
    .pipe(notify({ message: 'IMG task complete'}));
});

gulp.task('release', ['clean', 'all', 'release-img', 'release-js', 'release-css', 'release-fonts', 'release-lib'], function () {
  var sources = gulp.src([config.buildDir + config.js + '/**/*.js',
      config.buildDir + config.css + '/**/*.css'], {read: false});
  return gulp.src('app/index.html')
    .pipe(gInject(sources, {
      relative: false,
      ignorePath: config.buildDir,
      addRootSlash: false
    }))
    .pipe(gulp.dest(config.buildDir));
});

gulp.task('assets', ['icons', 'css', 'js', 'bower', 'lint', 'image']);
gulp.task('all', ['assets', 'template', 'index']);
gulp.task('default', ['watch-mode', 'all']);
gulp.task('server', ['connect', 'default']);
gulp.task('build', ['all', 'clean', 'release']);
gulp.task('test', ['debug', 'connect', 'all']);
